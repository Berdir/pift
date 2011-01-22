<?php
// $Id$

/**
 * @file
 * Provide cron functions.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Mark files for re-testing if they still meet all the criteria and the
 * re-test interval has passed.
 */
function pift_cron_retest() {
  if (PIFT_RETEST == -1) {
    return;
  }

  $api_versions = pift_core_api_versions();
  $sids = variable_get('pift_status', array());

  // Only attempt query if both values are not empty.
  if ($api_versions && $sids) {
    $retest_time = time() - PIFT_RETEST;

    // Loop over 'ON' clause to remove an 'OR' which should be more efficient.
    foreach (array('u.nid = pi.nid', 'cu.nid = pi.nid') as $clause) {
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
                    ON " . $clause . "
                  JOIN {pift_project} p
                    ON pi.pid = p.pid
                  JOIN {project_release_nodes} r
                    ON pi.rid = r.nid

                  JOIN {node} n
                    ON r.nid = n.nid
                  JOIN {term_node} t
                    ON (n.vid = t.vid AND t.tid IN (" . db_placeholders($api_versions, 'int') . "))

                  WHERE pi.sid IN (" . db_placeholders($sids, 'int') . ")
                )
                AND status > %d
                AND last_tested < %d",
                array_merge(array(PIFT_STATUS_QUEUE, PIFT_TYPE_FILE), $api_versions, $sids,
                            array(PIFT_STATUS_SENT, $retest_time)));
    }
  }
}

/**
 * Queue testing of releases/branches whenever a commit is made involving them.
 *
 * @param $min
 *   Min CID.
 * @param $max
 *   Max CID.
 */
function pift_cron_test_release($min, $max) {
  $result = db_query('SELECT nid AS pid, branch
                      FROM {cvs_files}
                      WHERE cid >= :min
                      AND cid <= :max
                      GROUP BY nid', array(':min' => $min, ':max' => $max));
  $commits = array();
  foreach ($result->fetchAll(PDO::FETCH_ASSOC) as $branch) {
    if (!pift_project_enabled($branch['pid'])) {
      // Ignore commits for disabled projects.
      continue;
    }

    // If commit group for project has not been started then initialize one.
    if (!isset($commits[$branch['pid']])) {
      $commits[$branch['pid']] = array();
    }

    // Use associative array to ensure that a branch is only added once.
    $identifier = $branch['branch'] ? $branch['branch'] : 'HEAD';
    $commits[$branch['pid']][$identifier] = $identifier;
  }

  $api_versions = pift_core_api_versions();
  foreach ($commits as $pid => $branches) {
    $rids = pift_cron_get_release($pid, $branches);
    foreach ($rids as $rid) {
      // Ensure that one of the compatibility terms is present on the release
      // node.
      $release = node_load($rid);
      $found = FALSE;
      foreach ($api_versions as $api_version) {
        if (array_key_exists($api_version, $release->taxonomy)) {
          // Compatible term found, continue processing.
          $test_id = db_query('SELECT test_id
                                         FROM {pift_test}
                                         WHERE type = :type
                                         AND id = :id', array(':type' => PIFT_TYPE_RELEASE, ':id' => $rid))->fetchField();

          // If existing test for release, queue it, otherwise add a new test.
          if ($test_id) {
            pift_test_requeue($test_id);
          }
          else if ($test_id !== 0) {
            pift_test_add(PIFT_TYPE_RELEASE, $rid);
          }
          break;
        }
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
      break;
    }
    elseif (isset($response['response'])) {
      watchdog('pift', 'Test queue request did not succeed: @message.',
        array('@message' => pift_cron_response_code($response['response'])), WATCHDOG_ERROR);
      break;
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
      watchdog('pift', 'Invalid response to test queue request.', $response, WATCHDOG_ERROR);
      break;
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
  $result = db_query_range('SELECT f.fid, f.filepath, u.nid AS u_nid, cu.nid AS c_nid, cu.cid AS c_cid
                      FROM {pift_test} t
                      JOIN {files} f
                        ON (t.type = :type AND t.id = f.fid)
                      LEFT JOIN {upload} u
                        ON f.fid = u.fid
                      LEFT JOIN {comment_upload} cu
                        ON f.fid = cu.fid
                      WHERE t.status = :status', 0, PIFT_XMLRPC_MAX, array(':type' => PIFT_TYPE_FILE, ':status' => PIFT_STATUS_QUEUE));
  foreach ($result->fetchAll(PDO::FETCH_ASSOC) as $file) {
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
  $result = db_query_range('SELECT t.id AS rid
                      FROM {pift_test} t
                      WHERE t.type = :type
                      AND t.status = :status', 0, PIFT_XMLRPC_MAX, array(':type' => PIFT_TYPE_RELEASE, ':status' =>PIFT_STATUS_QUEUE));
  foreach ($result->fetchCol() as $rid) {
    $branches[$rid] = TRUE;
  }
}

/**
 * Process the required branches and add related projects.
 *
 * @param array $batch Batch information.
 * @param array $branches Branches that must be loaded.
 * @param array $projects List of required projects (pids).
 * @param integer $depth (Internal) Number of times the function has been
 *   recursively executed.
 */
function pift_cron_queue_batch_build_branches_process(array &$batch, array &$branches, array &$projects, $depth = 0) {
  $api_versions = pift_core_api_versions();

  // Include branch data for all dependency branches and those that require review.
  $branches_implied = array();
  foreach ($branches as $rid => $test) {
    // Load branch release node.
    if (!($branch = node_load($rid))) {
      watchdog('pift', 'Invalid release ID [' . $rid . '] for test ID [' . $test['test_id'] . '].', WATCHDOG_ERROR);
      continue;
    }

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
    $api = array();
    foreach ($branch->taxonomy as $tid => $term) {
      if (in_array($tid, $api_versions)) {
        $api['version'] = array_shift(explode('.', $term->name, 2));
        $api['tid'] = $term->tid;
        break;
      }
    }

    // If the API version not found then ignore this branch.
    if (empty($api)) {
      watchdog('pift', 'Project release node [@nid] does not have a Drupal core API taxonomy term.', array('@nid' => $branch->nid), WATCHDOG_ERROR);
      continue;
    }

    // If the project is Drupal core then add the plugin argument, otherwise
    // determine the compatible core branch and add it as a dependency.
    if (PIFT_PID == $item['project_identifier']) {
      $item['plugin_argument']['drupal.core.version'] = $api['version'];
    }
    else {
      // Load the Drupal core API release (branch) compatible with this branch.
      $api_release = node_load(pift_core_api_release($api['tid']));
      $item['dependency'] = array($api_release->nid => $api_release->nid);

      // Cycle through dependencies and add the related branches to the implied
      // branch list if they are not already being processed.
      $dependencies = project_info_dependency_list_get($branch->nid);
      foreach ($dependencies as $dependency) {
        $item['dependency'][$dependency['rid']] = $dependency['rid'];
      }

      // Check for branches as dependencies that are not in the list of
      // processed branches.
      foreach ($item['dependency'] as $rid) {
        // Add branch to implied list for post-processing.
        if (!isset($branches[$rid])) {
          $branches_implied[$rid] = FALSE;
        }
      }

      // Ensure that not depending on self.
      unset($item['dependency'][$branch->nid]);

      // Flatten array of dependencies and fill in modules list.
      $item['dependency'] = implode(',', $item['dependency']);
      $item['plugin_argument']['drupal.modules'] = array_keys(project_info_module_list_get($branch->nid));
    }

    // Add item information to the batch.
    $batch['branches'][] = $item;

    // Add branch's project to list to be loaded.
    $projects[$branch->project_release['pid']] = $branch->project_release['pid'];
  }

  // Post-process any implied branches.
  if ($branches_implied && $depth == 0) {
    pift_cron_queue_batch_build_branches_process($batch, $branches_implied, $projects, $depth + 1);
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

    // Load repository information.
    $repository = $repositories[$project->cvs['repository']];

    // Generate project information.
    $item = array(
      'client_identifier' => $project->nid,
      'name' => $project->title,
      'repository_type' => 'cvs',
      'repository_url' => rtrim($repository->root . '/' . $repository->modules . $project->cvs['directory'], '/'),
      'link' => url('node/' . $project->nid, array('absolute' => TRUE)),
    );

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
  return db_query('SELECT nid
                      FROM {project_release_nodes}
                      WHERE pid = :pid
                      AND tag IN (:tags)',
                      array(':pid' => $pid, ':tags' => $tags))->fetchCol();
}

/**
 * Retrieve test results since the last cron run.
 */
function pift_cron_retrieve_results() {
  $next = PIFT_LAST_RETRIEVE;
  while ($next) {
    $response = xmlrpc(PIFT_SERVER . 'xmlrpc.php', 'pifr.retrieve', PIFT_KEY, (int) $next);
    $next = FALSE;

    if ($response === FALSE) {
      watchdog('pift', 'Failed to retrieve test results from to server: @message',
                       array('@message' => xmlrpc_error_msg()), WATCHDOG_ERROR);
    }
    elseif (isset($response['response'])) {
      if ($response['response'] != PIFR_RESPONSE_ACCEPTED) {
        watchdog('pift', 'Retrieval of test results did not succeed: @message.',
               array('@message' => pift_cron_response_code($response['response'])), WATCHDOG_ERROR);
      }
    }
    elseif (isset($response['results'])) {
      foreach ($response['results'] as $result) {
        db_update('pift_test')
          ->fields(array(
            'status' => $result['pass'] ? PIFT_STATUS_PASS : PIFT_STATUS_FAIL,
            'message' => $result['message'],
            'last_tested' => REQUEST_TIME,
          ))
          ->condition('test_id', $result['test_id'])
          ->execute();
        pift_cron_check_auto_followup($result);
      }

      // If there are more results then set $next to the last timestamp to be
      // used in next query.
      $next = $response['next'] ? $response['last'] : FALSE;

      // Always store last timestamp for next cron run.
      variable_set('pift_last_retrieve', $response['last']);
    }
    else {
      watchdog('pift', 'Invalid response to retrieval of test results.', array(), WATCHDOG_ERROR);
    }
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
      // Get the current issue state and ensure that the test is the last one
      // for the particular issue. Cycle through clauses to remove the need for
      // an OR clause and thus improve performance.
      $results = array();
      foreach (array('u.nid = i.nid', 'cu.nid = i.nid') as $clause) {
        $results[] = db_query_range('SELECT t.test_id, t.id
                            FROM {pift_test} t
                            LEFT JOIN {upload} u
                              ON (t.type = :type AND t.id = u.fid)
                            LEFT JOIN {comment_upload} cu
                              ON (t.type = :type AND t.id = cu.fid)
                            JOIN {project_issues} i
                              ON ' . $clause . '
                            WHERE u.nid = :nid OR cu.nid = :nid
                            ORDER BY t.id DESC', 0, 1, array(':type' => PIFT_TYPE_FILE, ':nid' => $test['nid']))->fetchAssoc();
      }

      // To simulate OR clause and descending order by 'id' select the test_id
      // that has the highest 'id' value.
      $test_id = $results[0]['id'] > $results[1]['id'] ? $results[0]['test_id'] : $results[1]['test_id'];

      if ($test_id == $test['test_id'] && pift_test_check_criteria_issue(node_load($test['nid']))) {
        // Test is last one for the particular issue and still fits the criteria.
        pift_cron_auto_followup($test['nid'], $test['cid'], $test['filename']);
      }
    }
  }
}

/**
 * Create an auto followup comment on the specified node.
 *
 * @param integer $nid Node ID to post the followup on.
 * @param integer $cid Comment ID, if applicable, containing the failed test.
 * @param string $filename Name of file.
 */
function pift_cron_auto_followup($nid, $cid, $filename) {
  project_issue_add_auto_followup(array(
    'nid' => $nid,
    'sid' => PIFT_FOLLOWUP_FAIL,
    'comment' => theme('pift_auto_followup', array('type' => 'fail', 'nid' => $nid, 'cid' => $cid, 'filename' => $filename)),
  ));
}

/**
 * Get the response from the code.
 *
 * @param integer $response_code Response code.
 * @return string Representation of reponse code.
 */
function pift_cron_response_code($response_code) {
  switch ($response_code) {
    case PIFR_RESPONSE_ACCEPTED:
      return t('Accepted');
    case PIFR_RESPONSE_INVALID_SERVER:
      return t('Invalid server (client key may have been disabled)');
    case PIFR_RESPONSE_DENIED:
      return t('Denied - ensure that test master is enabled and/or has reporting on');
    default:
      return 'Unknown';
  }
}
