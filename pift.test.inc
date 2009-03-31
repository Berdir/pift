<?php
// $Id$
/**
 * @file
 * Provide test functions.
 *
 * Copyright 2008-2009 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Get the test with the specified ID.
 *
 * @param integer $test_id Test ID.
 * @return array Test data.
 */
function pift_test_get($test_id) {
  return db_fetch_array(db_query('SELECT t.*, f.fid, f.filename, f.filepath, f.filesize,
                                     IF(u.nid IS NULL, cu.nid, u.nid) AS nid, cu.cid
                                   FROM {pift_test} t
                                   LEFT JOIN {files} f
                                     ON (t.type = %d AND t.id = f.fid)
                                   LEFT JOIN {upload} u
                                     ON f.fid = u.fid
                                   LEFT JOIN {comment_upload} cu
                                     ON f.fid = cu.fid
                                   WHERE t.test_id = %d', PIFT_TYPE_FILE, $test_id));
}

/**
 * Get the files and test data for the specified comment ID.
 *
 * @param integer $cid Comment ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_comment($cid) {
  $result = db_query('SELECT f.fid, f.filename, f.filepath, f.filesize, cu.nid, cu.cid, t.*
                      FROM {files} f
                      INNER JOIN {comment_upload} cu
                        ON f.fid = cu.fid
                      LEFT JOIN {pift_test} t
                        ON (t.type = %d AND f.fid = t.id)
                      WHERE cu.cid = %d
                      ORDER BY cu.weight, f.fid', PIFT_TYPE_FILE, $cid);
  $files = array();
  while ($file = db_fetch_array($result)) {
    $files[] = $file;
  }
  return $files;
}

/**
 * Get the files and test data on comments for the specified node ID.
 *
 * @param integer $nid Node ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_comment_all($nid) {
  $result = db_query('SELECT f.fid, f.filename, f.filepath, f.filesize, cu.nid, cu.cid, t.*
                      FROM {files} f
                      INNER JOIN {comment_upload} cu
                        ON f.fid = cu.fid
                      LEFT JOIN {pift_test} t
                        ON (t.type = %d AND f.fid = t.id)
                      WHERE cu.nid = %d
                      ORDER BY cu.weight, f.fid', PIFT_TYPE_FILE, $nid);
  $files = array();
  while ($file = db_fetch_array($result)) {
    $files[] = $file;
  }
  return $files;
}


/**
 * Get the files and test data for the specified node ID.
 *
 * @param integer $nid Node ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_node($nid) {
  $result = db_query('SELECT f.fid, f.filename, f.filepath, f.filesize, u.nid, t.*
                      FROM {files} f
                      INNER JOIN {upload} u
                        ON f.fid = u.fid
                      LEFT JOIN {pift_test} t
                        ON (t.type = %d AND f.fid = t.id)
                      WHERE u.nid = %d
                      ORDER BY u.weight, f.fid', PIFT_TYPE_FILE, $nid);
  $files = array();
  while ($file = db_fetch_array($result)) {
    $files[] = $file;
  }
  return $files;
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
 * @param object $node Node object.
 * @return boolean Passed criteria.
 */
function pift_test_check_criteria_issue($node) {
  if (!pift_project_enabled($node->project_issue['pid'])) {
    return FALSE;
  }

  $release = node_load($node->project_issue['rid']);
  if (!preg_match(pift_test_release_regex(FALSE), $release->project_release['tag'])) {
    return FALSE;
  }

  if (!in_array($node->project_issue['sid'], variable_get('pift_status', array()))) {
    return FALSE;
  }

  return TRUE;
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
 * @param integer $type The type of test to add, PIFT_TYPE_*.
 * @param integer $id Related test detail record ID, either rid, or fid.
 */
function pift_test_add($type, $id) {
  db_query('INSERT INTO {pift_test} (type, id, status)
            VALUES (%d, %d, %d)', $type, $id, PIFT_STATUS_QUEUE);
}

function pift_test_sent($test_id, $type, $id) {
  db_query('UPDATE {pift_test}
            SET test_id = %d,
            status = %d,
            WHERE type = %d
            AND id = %d', $test_id, PIFT_STATUS_SENT, $type, $id);
}

/**
 * Add a test back to the queue.
 *
 * @param integer $test_id Test ID.
 */
function pift_test_requeue($test_id) {
  db_query('UPDATE {pift_test}
            SET status = %d
            WHERE test_id = %d', PIFT_STATUS_QUEUE, $test_id);
}

/**
 * Clean up data since master records are removed before PIFT has a chance to
 * remove its related data.
 */
function pift_test_delete_files() {
  db_query('DELETE FROM {pift_test}
            WHERE type = %d
            AND id NOT IN (
              SELECT fid
              FROM {files}
            )', PIFT_TYPE_FILE);
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

/**
 * Generate a regular expression to match the release tags per specified core
 * versions.
 *
 * @param boolean $mysql_compatible Generate MySQL compatible regular expression.
 * @return string Regular expression.
 */
function pift_test_release_regex($mysql_compatible) {
  $regex = '';
  $branches = variable_get('pift_core', array());
  sort($branches);

  if (end($branches) == 7) {
    $regex .= 'HEAD|';
    array_pop($branches);
  }

  $branches = implode('|', $branches);
  return $mysql_compatible ? "{$regex}DRUPAL-($branches)" : "/{$regex}DRUPAL\-($branches)/";
}
