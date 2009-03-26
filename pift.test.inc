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
  return db_fetch_array(db_query('SELECT f.*, r.*, pf.test_id AS has_test, t.*
                                   FROM {pift_test} t
                                   LEFT JOIN {pift_file} pf
                                     ON t.test_id = pf.test_id
                                   LEFT JOIN {pift_release} pr
                                     ON t.test_id = pr.test_id
                                   LEFT JOIN {files} f
                                     ON pf.fid = f.fid
                                   LEFT JOIN {project_release_nodes} r
                                     ON pr.rid = r.nid
                                   WHERE t.test_id = %d', $test_id));
}

/**
 * Get the files and test data for the specified comment ID.
 *
 * @param integer $cid Comment ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_comment($cid) {
  $result = db_query('SELECT f.*, cu.*, pf.test_id AS has_test, t.*
                      FROM {files} f
                      INNER JOIN {comment_upload} cu
                        ON f.fid = cu.fid
                      LEFT JOIN {pift_file} pf
                        ON f.fid = pf.fid
                      LEFT JOIN {pift_test} t
                        ON pf.test_id = t.test_id
                      WHERE cu.cid = %d
                      ORDER BY cu.weight, f.fid', $cid);
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
  $result = db_query('SELECT f.*, cu.*, pf.test_id AS has_test, t.*
                      FROM {files} f
                      INNER JOIN {comment_upload} cu
                        ON f.fid = cu.fid
                      LEFT JOIN {pift_file} pf
                        ON f.fid = pf.fid
                      LEFT JOIN {pift_test} t
                        ON pf.test_id = t.test_id
                      WHERE cu.nid = %d
                      ORDER BY cu.weight, f.fid', $nid);
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
  $result = db_query('SELECT f.*, u.*, pf.test_id AS has_test, t.*
                      FROM {files} f
                      INNER JOIN {upload} u
                        ON f.fid = u.fid
                      LEFT JOIN {pift_file} pf
                        ON f.fid = pf.fid
                      LEFT JOIN {pift_test} t
                        ON pf.test_id = t.test_id
                      WHERE u.nid = %d
                      ORDER BY u.weight, f.fid', $nid);
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
    if (pift_test_check_criteria_file((array) $file)) {
      pift_test_add_file((array) $file);
    }
  }
}

/**
 * Add a file to the test tables.
 *
 * @param array $file File to add.
 */
function pift_test_add_file(array $file) {
  db_query('INSERT INTO {pift_file} (fid)
            VALUES (%d)', $file['fid']);
}

/**
 * Add a release to the test tables.
 *
 * @param integer $rid Release ID.
 */
function pift_test_add_release($rid) {
  db_query('INSERT INTO {pift_release} (rid)
            VALUES (%d)', $release['rid']);
}

/**
 * Update the test and related detail record with the assigned test ID and
 * current timestamp.
 *
 * @param integer $test_id Test ID.
 * @param string $type Test type.
 * @param integer $id ID for specified test type.
 */
function pift_test_add($test_id, $type, $id) {
  db_query('REPLACE INTO {pift_test} (test_id, status, timestamp)
            VALUES (%d, %d, %d)', $test_id, PIFT_STATUS_QUEUE, time());

  if ($type == 'branch') {
    db_query('UPDATE {pift_release}
              SET test_id = %d
              WHERE rid = %d', $test_id, $id);
  }
  else if ($type == 'file') {
    db_query('UPDATE {pift_file}
              SET test_id = %d
              WHERE fid = %d', $test_id, $id);
  }
}

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
            WHERE test_id IN (
              SELECT pf.test_id
              FROM {pift_file} pf
              LEFT JOIN {files} f
                ON pf.fid = f.fid
              WHERE f.fid IS NULL
            )');

  db_query('DELETE FROM {pift_file}
            WHERE fid NOT IN (
              SELECT fid
              FROM {files}
            )');
}

/**
 * Add previously submitted files once the node meets the criteria.
 *
 * @param integer $nid Node ID.
 */
function pift_test_add_previous_files($nid) {
  $files = pift_test_get_files_node_all($nid);
  dpm($files);
  foreach ($files as $file) {
    $file = (array) $file;
    if ($file['has_test'] === NULL && pift_test_check_criteria_file($file)) {
      pift_test_add_file($file);
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
