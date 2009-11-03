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

  $api_versions = array_filter(variable_get('pift_core', array()));
  $sids = variable_get('pift_status', array());

  // Only attempt query if both values are not empty.
  if ($api_versions && $sids) {
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

                JOIN {node} n
                  ON r.nid = n.nid
                JOIN {term_node} t
                  ON (n.vid = t.vid AND t.tid IN(" . db_placeholders($api_versions, 'int') . "))

                WHERE pi.sid IN (" . db_placeholders($sids, 'int') . ")
              )
              AND status > %d
              AND last_tested < %d",
              array_merge(array(PIFT_STATUS_QUEUE, PIFT_TYPE_FILE), $api_versions, $sids,
                          array(PIFT_STATUS_SENT, $retest_time)));
  }
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
  $i = 1;
  while (($batch = pift_cron_queue_batch_build()) && $batch['projects']) {
    $response = xmlrpc(PIFT_SERVER . 'xmlrpc.php', 'pifr.queue', PIFT_KEY, $batch);
    if ($response === FALSE) {
      watchdog('pift', 'Failed to send test queue requests to server.', array(), WATCHDOG_ERROR);
    }
    elseif (isset($response['response'])) {
      watchdog('pift', 'Test queue request did not succeed: @code.', array('@code' => $response['response']), WATCHDOG_ERROR);
    }
    elseif (isset($response['branches']) && isset($response['files'])) {
      // Store branch test IDs using the client_identifier (branch NID).
      foreach ($batch['branches'] as $branch) {
        if (isset($response['branches'][$branch['client_identifier']])) {
          pift_test_sent($response['branches'][$branch['client_identifier']], PIFT_TYPE_RELEASE, $branch['client_identifier']);
        }
      }

      // Store file test IDs using the client_identifier (file ID).
      foreach ($batch['files'] as $file) {
        if (isset($response['files'][$file['client_identifier']])) {
          pift_test_sent($response['files'][$file['client_identifier']], PIFT_TYPE_FILE, $file['client_identifier']);
        }
      }
    }
    else {
      watchdog('pift', 'Invalid response to test queue request.', array(), WATCHDOG_ERROR);
    }

    // Once the maximum number of batches has been sent break out of the loop
    // and save the rest for the next cron run.
    if ($i++ == PIFT_XMLRPC_MAX_BATCHES) {
      break;
    }
  }
}

/**
 * Build a batch of tests to queue.
 *
 * @return array Complete batch array as defined by pifr.queue().
 */
function pift_cron_queue_batch_build() {
  // Initialize basic batch structure.
  $batch = array(
    'branches' => array(),
    'files' => array(),
    'projects' => array(),
  );

  // Provide arrays to keep track of the required branches and projects.
  $branches = array();
  $projects = array();

  // Load files that need to be reviewed.
  pift_cron_queue_batch_build_files($batch, $branches);

  // Load branches that need to be reviewed.
  pift_cron_queue_batch_build_branches($branches);

  // Process the required branches and add related projects.
  pift_cron_queue_batch_build_branches_process($batch, $branches, $projects);

  // Generate project information for each required project.
  pift_cron_queue_batch_build_projects($batch, $projects);

  return $batch;
}

/**
 * Load files that need to be reviewed and add them to the batch.
 *
 * @param array $batch Batch information.
 * @param array $branches Branches that must be loaded.
 */
function pift_cron_queue_batch_build_files(array &$batch, array &$branches) {
  // Load all files that are marked as needs testing or have never been tested.
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
    // Load the issue related to the file, either from the comment or node.
    $issue = node_load($file['u_nid'] ? $file['u_nid'] : $file['c_nid']);

    // Generate item information.
    $item = array(
      'branch_identifier' => $issue->project_issue['rid'],
      'client_identifier' => $file['fid'],
      'file_url' => file_create_url($file['filepath']),
    );

    // Generate link to file issue and comment if relevant.
    if ($file['c_cid']) {
      $item['link'] = url('node/' . $issue->nid, array('absolute' => TRUE, 'fragment' => 'comment-' . $file['c_cid']));
    }
    else {
      $item['link'] = url('node/' . $issue->nid, array('absolute' => TRUE));
    }

    // Add file to batch.
    $batch['files'][] = $item;

    // Store branch as needed to be included with data.
    $branches[$issue->project_issue['rid']] = FALSE; // Do not test unless commit found.
  }
}

/**
 * Load branches that need to be reviewed.
 *
 * @param array $branches Branches that must be loaded.
 */
function pift_cron_queue_batch_build_branches(array &$branches) {
  // Load the branches that are marked as needs testing or have never been tested.
  $result = db_query('SELECT t.id AS rid
                      FROM {pift_test} t
                      WHERE t.type = %d
                      AND t.status = %d
                      LIMIT %d', PIFT_TYPE_RELEASE, PIFT_STATUS_QUEUE, PIFT_XMLRPC_MAX);
  while ($branch = db_fetch_array($result)) {
    $branches[$branch['rid']] = TRUE;
  }
}

/**
 * Process the required branches and add related projects.
 *
 * @param array $batch Batch information.
 * @param array $branches Branches that must be loaded.
 * @param array $projects List of required projects (pids).
 */
function pift_cron_queue_batch_build_branches_process(array &$batch, array &$branches, array &$projects) {
  // Relase API version vocabulary.
  $api_vid = _project_release_get_api_vid();

  // Include branch data for all dependency branches and those that require review.
  foreach ($branches as $rid => $test) {
    // Load branch release node.
    $branch = node_load($rid);

    // Generate branch information.
    $item = array(
      'project_identifier' => $branch->project_release['pid'],
      'client_identifier' => $branch->nid,
      'vcs_identifier' => $branch->project_release['tag'],
      'dependency' => '',
      'plugin_argument' => array(),
      'test' => $test,
      'link' => url('node/' . $branch->nid, array('absolute' => TRUE)),
    );

    // Attempt to determine the Drupal core API version.
    list($api_version, $api_tid) = pift_core_api($branch);

    // If the API version not found then ignore this branch.
    if (empty($api_version)) {
      watchdog('pift', 'Project release node [@nid] must have a Drupal core API taxonomy term.', array('@nid' => $branch->nid));
      continue;
    }

    // If the project is Drupal core then add the plugin argument, otherwise
    // determine the compatible core branch and add it as a dependency.
    if (PIFT_PID == $item['project_nid']) {
      $item['plugin_argument']['core'] = $api_version;
    }
    else {
      // Load the Drupal core API release (branch) compatible with this branch.
      $api_release = node_load(pift_core_api_release($api_tid));
      $item['dependency'] = $api_release->nid;
      $item['plugin_argument']['modules'] = array(); // TODO Include the list of modules.

      // If the core branch has not already been added then add it.
      if (!isset($branches[$api_release])) {
        $branches[$api_release->nid] = FALSE;
        $projects[PIFT_PID] = PIFT_PID;
      }
    }

    // Add item information to the batch.
    $batch['branches'][] = $item;

    // Add branch's project to list to be laoded.
    $projects[$branch->project_release['pid']] = $branch->project_release['pid'];
  }
}

/**
 * Generate project information for each required project.
 *
 * @param array $batch Batch information.
 * @param array $projects List of required projects (pids).
 */
function pift_cron_queue_batch_build_projects(array &$batch, array $projects) {
  // Load CVS repositories.
  $repositories = cvs_get_repository_info();

  // Cycle through each required project.
  foreach ($projects as $pid) {
    // Load project node.
    $project = node_load($pid);

    // Generate project information.
    $item = array(
      'client_identifier' => $project->nid,
      'name' => $project->title,
      'repository_type' => 'cvs',
      'repository_url' => $repositories[$project->cvs['repository']]->root . $project->cvs['directory'],
      'link' => url('node/' . $project->nid, array('absolute' => TRUE)),
    );

    if ($project->nid == PIFT_PID) {
      // Drupal core directory is set to '/' and needs to be '/drupal'.
      $item['repository_url'] .= 'drupal';
    }

    $batch['projects'][] = $item;
  }
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
                      AND tag IN (' . db_placeholders($tags, 'varchar') . ')',
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
  $last = db_result(db_query('SELECT MAX(last_tested) FROM {pift_test}'));
  $next = pifr_server_xmlrpc_time_gmt($last ? $last : 0);
  while ($next) {
    $response = xmlrpc(PIFT_SERVER . 'xmlrpc.php', 'pifr.retrieve', PIFT_KEY, $next);
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
