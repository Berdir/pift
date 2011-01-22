<?php
// $Id$

/**
 * @file
 * Provide test functions.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Get the test with the specified ID.
 *
 * @param $test_id
 *   Test ID.
 * @return
 *   Test data.
 */
function pift_test_get($test_id) {
  return db_query('SELECT t.*, f.fid, f.filename, f.filepath, f.filesize,
                       IF(u.nid IS NULL, cu.nid, u.nid) AS nid, cu.cid
                     FROM {pift_test} t
                     LEFT JOIN {files} f
                       ON (t.type = :type AND t.id = f.fid)
                     LEFT JOIN {upload} u
                       ON f.fid = u.fid
                     LEFT JOIN {comment_upload} cu
                       ON f.fid = cu.fid
                     WHERE t.test_id = :test_id', array(':type' => PIFT_TYPE_FILE, ':test_id' => $test_id))->fetchAssoc();
}

/**
 * Get the files and test data for the specified comment ID.
 *
 * @param $cid
 *   Comment ID.
 * @return
 *   List of files and test data.
 */
function pift_test_get_files_comment($cid) {
  return db_query('SELECT f.fid, f.filename, f.filepath, f.filesize, cu.nid, cu.cid, t.*
                      FROM {files} f
                      INNER JOIN {comment_upload} cu
                        ON f.fid = cu.fid
                      LEFT JOIN {pift_test} t
                        ON (t.type = :type AND f.fid = t.id)
                      WHERE cu.cid = :cid
                      ORDER BY cu.weight, f.fid', array(':type' => PIFT_TYPE_FILE, ':cid' => $cid))->fetchAll(PDO::FETCH_ASSOC);
}

/**
 * Get the files and test data on comments for the specified node ID.
 *
 * @param integer $nid Node ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_comment_all($nid) {
  return db_query('SELECT f.fid, f.filename, f.filepath, f.filesize, cu.nid, cu.cid, t.*
                      FROM {files} f
                      INNER JOIN {comment_upload} cu
                        ON f.fid = cu.fid
                      LEFT JOIN {pift_test} t
                        ON (t.type = :type AND f.fid = t.id)
                      WHERE cu.nid = :nid
                      ORDER BY cu.weight, f.fid', array(':type' => PIFT_TYPE_FILE, ':nid' => $nid))->fetchAll(PDO::FETCH_ASSOC);
}


/**
 * Get the files and test data for the specified node ID.
 *
 * @param integer $nid Node ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_node($nid) {
  return db_query('SELECT f.fid, f.filename, f.filepath, f.filesize, u.nid, t.*
                      FROM {files} f
                      INNER JOIN {upload} u
                        ON f.fid = u.fid
                      LEFT JOIN {pift_test} t
                        ON (t.type = :type AND f.fid = t.id)
                      WHERE u.nid = :nid
                      ORDER BY u.weight, f.fid', array(':type' => PIFT_TYPE_FILE, ':nid' => $nid))->fetchAll(PDO::FETCH_ASSOC);
}


/**
 * Get the files and test data for the specified node ID and its comments.
 *
 * @param integer $nid Node ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_node_all($nid) {
  return array_merge(pift_test_get_files_node($nid), pift_test_get_files_comment_all($nid));
}

/**
 * Check the criteria for the specified issue.
 *
 * The checks are in order of expensive.
 *
 * @param object $node Node object.
 * @return boolean Passed criteria.
 */
function pift_test_check_criteria_issue($node) {
  // Ensure that the issue is in one of the acceptable statuses.
  if (!in_array($node->project_issue['sid'], variable_get('pift_status', array()))) {
    return FALSE;
  }

  // Ensure that the project has testing enabled.
  if (!pift_project_enabled($node->project_issue['pid'])) {
    return FALSE;
  }

  // Ensure that one of the compatibility terms is present on the release node.
  $release = node_load($node->project_issue['rid']);
  $api_versions = pift_core_api_versions();
  foreach ($api_versions as $api_version) {
    if (array_key_exists($api_version, $release->taxonomy)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * Check the criteria for the specified file.
 *
 * @param array $file File to check.
 * @return boolean Passed criteria.
 */
function pift_test_check_criteria_file(array $file) {
  if (!preg_match(PIFT_REGEX, $file['filename'])) {
    return FALSE;
  }
  return TRUE;
}

/**
 * Add files to test tables.
 *
 * @param array $files Files to add.
 */
function pift_test_add_files(array $files) {
  foreach ($files as $file) {
    $file = (array) $file;
    if (pift_test_check_criteria_file($file)) {
      pift_test_add(PIFT_TYPE_FILE, $file['fid']);
    }
  }
}

/**
 * Add a test to the test tables.
 *
 * @param $type
 *   The type of test to add, PIFT_TYPE_*.
 * @param $id
 *   Related test detail record ID, either rid, or fid.
 */
function pift_test_add($type, $id) {
  db_insert('pift_test')
    ->fields(array(
      'type' => $type,
      'id' => $id,
      'status' => PIFT_STATUS_QUEUE,
    ))
    ->execute();
}

function pift_test_sent($test_id, $type, $id) {
  db_update('pift_test')
    ->fields(array(
      'test_id' => $test_id,
      'status' => PIFT_STATUS_SENT,
    ))
    ->condition('type', $type)
    ->condition('id', $id)
    ->execute();
}

/**
 * Add a test back to the queue.
 *
 * @param integer $test_id Test ID.
 */
function pift_test_requeue($test_id) {
  db_update('pift_test')
    ->fields(array('status' => PIFT_STATUS_QUEUE))
    ->condition('test_id', $test_id)
    ->execute();
}

/**
 * Clean up data since master records are removed before PIFT has a chance to
 * remove its related data.
 */
function pift_test_delete_files() {

  $subselect = db_select('files')
    ->fields('files', array('fid'));

  db_delete('pift_test')
    ->condition('type', PIFT_TYPE_FILE)
    ->condition('id', $subselect, 'NOT IN')
    ->execute();
}

/**
 * Add previously submitted files once the node meets the criteria.
 *
 * @param integer $nid Node ID.
 */
function pift_test_add_previous_files($nid) {
  $files = pift_test_get_files_node_all($nid);
  foreach ($files as $file) {
    $file = (array) $file;
    if ($file['test_id'] === NULL && pift_test_check_criteria_file($file)) {
      pift_test_add(PIFT_TYPE_FILE, $file['fid']);
    }
  }
}
