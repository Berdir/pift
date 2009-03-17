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
  return db_fetch_array(db_query('SELECT *
                                   FROM {pift_test} t
                                   LEFT JOIN {pift_file} pf
                                     ON t.test_id = pf.test_id
                                   LEFT JOIN {pift_release} pr
                                     ON t.test_id = pr.test_id
                                   LEFT JOIN {files} f
                                     ON pf.fid = f.fid
                                   LEFT JOIN {project_release_nodes} r
                                     ON pr.release_nid = r.nid
                                   WHERE t.test_id = %d', $test_id));
}

/**
 * Get the files and test data for the specified comment ID.
 *
 * @param integer $cid Comment ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_comment($cid) {
  $result = db_query('SELECT *
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
 * Get the files and test data for the specified node ID.
 *
 * @param integer $nid Node ID.
 * @return array List of files and test data.
 */
function pift_test_get_files_node($nid) {
  $result = db_query('SELECT *
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
