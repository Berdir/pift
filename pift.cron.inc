<?php
// $Id$
/**
 * @file
 * Provide cron functions.
 *
 * Copyright 2008-2009 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Mark files for re-testing if they still meet all the criteria and the
 * re-test interval has passed.
 */
function pift_cron_retest() {
  if (PIFT_RETEST == -1) {
    return;
  }

  $sids = variable_get('pift_status', array());
  $retest_time = time() - PIFT_RETEST;
  db_query("UPDATE {pift_test}
            SET status = %d
            WHERE type = %d
            AND id IN (
              SELECT f.fid
              FROM {files} f
              LEFT JOIN {upload} u
                ON f.fid = u.fid
              LEFT JOIN {comment_upload} cu
                ON f.fid = cu.fid

              JOIN {project_issues} pi
                ON (u.nid = pi.nid OR cu.nid = pi.nid)
              JOIN {pift_project} p
                ON pi.pid = p.pid
              JOIN {project_release_nodes} r
                ON pi.rid = r.nid

              WHERE pi.sid IN (" . implode(',', array_fill(0, count($sids), '%d')) . ")
              AND r.tag REGEXP '%s'
            )
            AND status > %d
            AND last_tested < %d",
            array_merge(array(PIFT_STATUS_QUEUE), array(PIFT_TYPE_FILE), $sids,
                        array(pift_test_release_regex(TRUE)), array(PIFT_STATUS_SENT), array($retest_time)));
}

/**
 * Queue testing of releases/branches whenever a commit is made involving them.
 *
 * @param integer $min Min CID.
 * @param integer $max Max CID.
 */
function pift_cron_test_release($min, $max) {
  $result = db_query('SELECT nid AS pid, branch
                      FROM {cvs_files}
                      WHERE cid >= %d
                      AND cid <= %d
                      GROUP BY nid', $min, $max);
  $commits = array();
  while ($branch = db_fetch_array($result)) {
    if (!pift_project_enabled($branch['pid'])) {
      // Ignore commits for disabled projects.
      continue;
    }

    if (!isset($commits[$branch['pid']])) {
      $commits[$branch['pid']] = array();
    }

    // Use associative array to ensure that a branch is only added once.
    $identifier = $branch['branch'] ? $branch['branch'] : 'HEAD';
    $commits[$branch['pid']][$identifier] = $identifier;
  }

  foreach ($commits as $pid => $branches) {
    $rids = pift_cron_get_release($pid, $branches);
    foreach ($rids as $rid) {
      $test_id = db_result(db_query('SELECT test_id
                                     FROM {pift_test}
                                     WHERE type = %d
                                     AND id = %d', PIFT_TYPE_RELEASE, $rid));
      if ($test_id) {
        pift_test_requeue($test_id);
      }
      else if ($test_id !== 0) {
        pift_test_add(PIFT_TYPE_RELEASE, $rid);
      }
    }
  }
}

/**
 * Send the all test batches to testing server.
 */
function pift_cron_queue_batch() {
  $i = 0;
  while ((list($files, $batch) = pift_cron_queue_batch_build()) && $batch['projects']) {
    $response = xmlrpc(PIFT_SERVER . '/xmlrpc.php', 'pifr.queue', PIFT_KEY, $batch);
    if ($response === FALSE) {
      watchdog('pift', 'Failed to send test queue requests to server.', array(), WATCHDOG_ERROR);
    }
    elseif (isset($response['response'])) {
      watchdog('pift', 'Test queue request did not succeed: @code.', array('@code' => $response['response']), WATCHDOG_ERROR);
    }
    elseif (isset($response['branches']) && isset($response['files'])) {
      // Store test records using assigned test IDs.
      foreach ($batch['branches'] as $key => $branch) {
        if (isset($response['branches'][$key])) {
          pift_test_sent($response['branches'][$key], PIFT_TYPE_RELEASE, $batch['branches'][$key]['branch_nid']);
        }
      }

      foreach ($response['files'] as $key => $file) {
        if (isset($response['files'][$key])) {
          pift_test_sent($response['files'][$key], PIFT_TYPE_FILE, $files[$key]);
        }
      }
    }
    else {
      watchdog('pift', 'Invalid response to test queue request.', array(), WATCHDOG_ERROR);
    }

    if (++$i == PIFT_XMLRPC_MAX_BATCHES) {
      // Once the maximum number of batches has been sent break out of the loop
      // and save the rest for the next cron run.
      break;
    }
  }
}

/**
 * Build the batch of tests to queue, using pifr.queue().
 *
 * @return array List of file IDs and batch of tests to queue.
 */
function pift_cron_queue_batch_build() {
  $batch = array(
    'branches' => array(),
    'files' => array(),
    'projects' => array(),
  );
  $branches = array();
  $files = array();
  $projects = array();

  // Test all files that are marked as needs testing or have never been tested.
  $result = db_query('SELECT f.fid, f.filepath, u.nid AS u_nid, cu.nid AS c_nid, cu.cid AS c_cid
                      FROM {pift_test} t
                      JOIN {files} f
                        ON (t.type = %d AND t.id = f.fid)
                      LEFT JOIN {upload} u
                        ON f.fid = u.fid
                      LEFT JOIN {comment_upload} cu
                        ON f.fid = cu.fid
                      WHERE t.status = %d
                      LIMIT %d', PIFT_TYPE_FILE, PIFT_STATUS_QUEUE, PIFT_XMLRPC_MAX);
  while ($file = db_fetch_array($result)) {
    $node_file = ($file['u_nid'] ? TRUE : FALSE);
    $issue = node_load(($node_file ? $file['u_nid'] : $file['c_nid']));

    $batch['files'][] = array(
      'branch_nid' => $issue->project_issue['rid'],
      'file_url' => file_create_url($file['filepath']),
      'issue_nid' => $issue->nid,
      'issue_cid' => ($file['c_cid'] ? $file['c_cid'] : 0),
    );
    $files[] = $file['fid'];
    $branches[$issue->project_issue['rid']] = FALSE; // Do not test unless commit found.
  }

  // Test the branches that are marked as needs testing or have never been tested.
  $result = db_query('SELECT t.id AS rid
                      FROM {pift_test} t
                      WHERE t.type = %d
                      AND t.status = %d
                      LIMIT %d', PIFT_TYPE_RELEASE, PIFT_STATUS_QUEUE, PIFT_XMLRPC_MAX);
  while ($branch = db_fetch_array($result)) {
    $branches[$branch['rid']] = TRUE;
  }

  // Include branch data for all dependency branches and those that require testing.
  foreach ($branches as $rid => $test) {
    $branch = node_load($rid);

    $item = array(
      'branch_nid' => $branch->nid,
      'project_nid' => $branch->project_release['pid'],
      'identifier' => $branch->project_release['tag'],
      'test' => $test,
    );
    if (PIFT_PID != $item['project_nid']) {
      // Branch is not core branch so find out what core branch it is compatible with.
      if (preg_match('/DRUPAL-(\d+)/', $item['identifier'], $match)) {
        // Get the core branch compatible with the numeric version.
        $rids = pift_cron_get_release(PIFT_PID, array($item['identifier']));
        if ($rids) {
          $item['core_branch_nid'] = $rid[0];
          $branches[] = $rid[0];
        }
      }
    }

    $batch['branches'][] = $item;
    $projects[] = $branch->project_release['pid'];
  }

  // Include project data for all branches.
  $repositories = cvs_get_repository_info();
  $projects = array_unique($projects);
  foreach ($projects as $project) {
    $project = node_load($project);
    $batch['projects'][] = array(
      'project_nid' => $project->nid,
      'name' => $project->title,
      'type' => ($project->nid == PIFT_PID ? PIFR_SERVER_PROJECT_TYPE_CORE : PIFR_SERVER_PROJECT_TYPE_MODULE),
      'repository_type' => 'cvs',
      'repository_url' => $repositories[$project->cvs['repository']]->root . $project->cvs['directory'],
    );
  }
  return array($files, $batch);
}

/**
 * Get the release NIDs that correspond to the specified project and tags.
 *
 * @param integer $pid Project NID.
 * @param array $tags List of tags.
 * @return array Release NIDs that match.
 */
function pift_cron_get_release($pid, $tags) {
  $result = db_query('SELECT nid
                      FROM {project_release_nodes}
                      WHERE pid = %d
                      AND tag IN (' . implode(',', array_fill(0, count($tags), "'%s'")) . ')',
                      array_merge(array($pid), $tags));
  $rids = array();
  while ($rid = db_result($result)) {
    $rids[] = $rid;
  }
  return $rids;
}

/**
 * Retrieve test results since the last cron run.
 */
function pift_cron_retreive_results() {
  $next = pifr_server_xmlrpc_time_gmt(PIFT_LAST);
  while ($next) {
    $response = xmlrpc(PIFT_SERVER . '/xmlrpc.php', 'pifr.retrieve', PIFT_KEY, $next);
    $next = FALSE;

    if ($response === FALSE) {
      watchdog('pift', 'Failed to retrieve test results from to server: @message',
                       array('@message' => xmlrpc_error_msg()), WATCHDOG_ERROR);
    }
    elseif (isset($response['response'])) {
      if ($response['response'] != PIFR_RESPONSE_ACCEPTED) {
        watchdog('pift', 'Retrieval of test results did not succeed: @code.',
               array('@code' => $response['response']), WATCHDOG_ERROR);
      }
      else {
        watchdog('pift', 'No test results to retrieve.');
      }
    }
    elseif (isset($response['results'])) {
      foreach ($response['results'] as $result) {
        db_query("UPDATE {pift_test}
                  SET status = %d,
                  message = '%s',
                  last_tested = %d
                  WHERE test_id = %d",
                  $result['pass'] ? PIFT_STATUS_PASS : PIFT_STATUS_FAIL, $result['message'], time(), $result['test_id']);

        pift_cron_check_auto_followup($result);
      }

      $next = isset($response['next']) ? $response['next'] : FALSE;
    }
    else {
      watchdog('pift', 'Invalid response to retrieval of test resutls.', array(), WATCHDOG_ERROR);
    }
  }
}

if (!function_exists('pifr_server_xmlrpc_time_local')) {
  /**
   * Convert a GMT Unix timestamp to a local Unix timestamp.
   *
   * @param integer $gmt_timestamp GMT Unix timestamp.
   * @return integer Local timestamp.
   */
  function pifr_server_xmlrpc_time_local($gmt_timestamp) {
    $gmt_now = gmmktime();
    $offset = $gmt_now - $gmt_timestamp;
    return time() - $offset;
  }

  /**
   * Convert a local Unix timestamp to a GMT Unix timestamp.
   *
   * @param integer $timestamp Local Unix timestamp.
   * @return integer GMT Unix timestamp
   */
  function pifr_server_xmlrpc_time_gmt($timestamp) {
    $now = time();
    $offset = $now - $timestamp;
    return gmmktime() - $offset;
  }
}

/**
 * Check if an auto followup comment is applicable and if so make one.
 *
 * @param array $result Test result from XML-RPC retrieval.
 */
function pift_cron_check_auto_followup(array $result) {
  if (!$result['pass']) {
    // Test did not pass, make sure that test is for a file.
    $test = pift_test_get($result['test_id']);
    if ($test['fid']) {
      // Get the current issue state and ensure that the test is the last one for
      // the particular issue.
      $test_id = db_result(db_query('SELECT t.test_id
                                     FROM {pift_test} t
                                     LEFT JOIN {upload} u
                                       ON (t.type = %d AND t.id = u.fid)
                                     LEFT JOIN {comment_upload} cu
                                       ON (t.type = %d AND t.id = cu.fid)
                                     JOIN {project_issues} i
                                       ON (u.nid = i.nid OR cu.nid = i.nid)
                                     WHERE u.nid = %d OR cu.nid = %d
                                     ORDER BY t.id DESC
                                     LIMIT 1', PIFT_TYPE_FILE, PIFT_TYPE_FILE, $test['nid'], $test['nid']));

      if ($test_id == $test['test_id'] && pift_test_check_criteria_issue(node_load($test['nid']))) {
        // Test is last one for the particular issue and still fits the criteria.
        pift_cron_auto_followup($test['nid'], $test['cid']);
      }
    }
  }
}

/**
 * Create an auto followup comment on the specified node.
 *
 * @param integer $nid Node ID to post the followup on.
 * @param integer $cid Comment ID, if applicable, containing the failed test.
 */
function pift_cron_auto_followup($nid, $cid) {
  project_issue_add_auto_followup(array(
    'nid' => $nid,
    'sid' => PIFT_FOLLOWUP_FAIL,
    'comment' => theme('pift_auto_followup', 'fail', $nid, $cid),
  ));
}
