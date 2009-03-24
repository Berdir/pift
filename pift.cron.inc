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
            WHERE test_id IN (
              SELECT pf.test_id
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

              JOIN {pift_file} pf
                ON (u.fid = pf.fid OR cu.fid = pf.fid)
              WHERE pi.sid IN (" . implode(',', array_fill(0, count($sids), '%d')) . ")
              AND r.tag REGEXP '%s'
            )
            AND last_tested < %d",
            array_merge(array(PIFT_STATUS_QUEUE), $sids, array(pift_test_release_regex(TRUE)), array($retest_time)));
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
    if (!isset($commits[$branch['pid']])) {
      $commits[$branch['pid']] = array();
    }
    $commits[$branch['pid']][] = ($branch['branch'] ? $branch['branch'] : 'HEAD');
  }

  foreach ($commits as $pid => $branches) {
    $rids = pift_cron_get_release($pid, $branches);
    foreach ($rids as $rid) {
      $test_id = db_result(db_query('SELECT test_id
                                     FROM {pift_release}
                                     WHERE rid = %d)', $rid));
      if ($test_id) {
        pift_test_requeue($test_id);
      }
      else if ($test_id !== 0) {
        pift_test_add_release($rid);
      }
    }
  }
}

/**
 * Send the all test batches to testing server.
 */
function pift_cron_queue_batch() {
  while ($batch = pift_cron_queue_batch_build()) {
    // TODO Send batch.
  }
}

/**
 * Build the batch of tests to queue, using pifr.queue().
 *
 * @return array Batch of tests to queue.
 */
function pift_cron_queue_batch_build() {
  $batch = array(
    'branches' => array(),
    'files' => array(),
    'projects' => array(),
  );
  $projects = array();
  $branches = array();

  // Test all files that are marked as needs testing or have never been tested.
  $result = db_query('SELECT pf.fid, f.filepath, u.nid AS u_nid, cu.nid AS c_nid, cu.cid AS c_cid
                      FROM {pift_file} pf
                      LEFT JOIN {pift_test} t
                        ON pf.test_id = t.test_id
                      JOIN {files} f
                        ON pf.fid = f.fid
                      LEFT JOIN {upload} u
                        ON pf.fid = u.fid
                      LEFT JOIN {comment_upload} cu
                        ON pf.fid = cu.fid
                      WHERE (t.status = 1 OR t.status IS NULL)');
  while ($file = db_fetch_array($result)) {
    $node_file = ($file['u_nid'] ? TRUE : FALSE);
    $issue = node_load(($node_file ? $file['u_nid'] : $file['c_nid']));

    $batch['files'][] = array(
      'branch_nid' => $issue->project_issue['rid'],
      'file_url' => url($file['filepath']),
      'issue_nid' => $issue->nid,
      'issue_cid' => ($file['c_cid'] ? $file['c_cid'] : 0),
    );
    $branches[$issue->project_issue['rid']] = FALSE; // Do not test unless commit found.
  }

  // Test the branches that are marked as needs testing or have never been tested.
  $result = db_query('SELECT *
                      FROM {pift_release} pr
                      LEFT JOIN {pift_test} t
                        ON pr.test_id = t.test_id
                      WHERE (t.status = 1 OR t.status IS NULL)');
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
      if (preg_match('/DRUPAL-(\d+)', $item['identifier'], $match)) {
        // Get the core branch compatible with the numeric version.
        $rids = pift_cron_get_release($pid, array($match[1]));
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
  return $batch;
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
 * Sends file data to test servers, in a round robin approach.
 *
 * The files will be sent via XML-RPC to any servers listed on the settings page,
 * which also have a valid Project issue file review module installed and configured
 * with the server key of the PIFT server (available on the PIFT server settings page).
 *
 * The files will be sent in an array, each element of which is an associative array
 * with the following key/value pairs:
 *
 *     'ftid'          => The file test ID that was originally passed from
 *                       the test server for the file in question.
 *
 *     'cid'           => The comment ID of the comment the patch is
 *                        attached to.
 *
 *     'pid'           => The project ID.
 *
 *     'rid'           => The release ID.
 *
 *     'uid'           => The uid of the user who submitted the file.
 *
 *     'issue_id'      => nid of the issue.
 *
 *     'issue_title'   => Title of the issue.
 *
 *     'project'       => Project the patch is for.
 *
 *     'version'       => Project version string.
 *
 *     'tag'           => Release tag as found in {project_release_nodes}.
 *
 *     'submitter'     => Drupal user submitting the patch.
 *
 *     'patch_url'     => The absolute URL for the file.
 */
function pift_send_file_data() {
  global $base_url;

  // Get servers if any exist.
  if ($all_server_data = variable_get('pift_sites', '')) {
    $all_server_data = explode("\n", $all_server_data);
    $servers = array();
    $i = 0;

    // Set up the test servers.
    foreach ($all_server_data as $server_data) {
      if ($server_data) {
        // Set up the login data for the server. The test server needs
        // it's server key for authentication.
        $server_data = explode('@', $server_data);
        $server['server_key'] = $server_data[0];
        $server['xmlrpc_url'] = $server_data[1];
        // Basic testing to make sure the XML-RPC URL isn't totally bogus.
        if (preg_match('/^(http|https):\/\/.+\/xmlrpc.php/', $server['xmlrpc_url'])) {
          $i++;
          $servers[$i] = $server;
        }
        // Bad server URL.
        else {
          watchdog('project_remote', '%server is an invalid URL.', array('%server' => $server_xmlrpc_url), WATCHDOG_ERROR);
        }
      }
    }
    if (empty($servers)) {
      return;
    }
  }
  else {
    return;
  }

  // Set up the resend time limit.
  $resend_time = time() - (PIFT_RESEND_TIME * 60 * 60);
  $retest_time = time() - (PIFT_RETEST * 60 * 60);

  $where = '';
  $where_args = array();
  $sub_query_args = array(PIFT_STATUS_UNTESTED, $resend_time, PIFT_STATUS_RETEST, $retest_time, 0);

  // Restrict to listed projects, if set.
  if ($projects = pift_project_list()) {
    $where .= ' AND p.nid IN ('. implode(',', array_fill(0, count($projects), '%d')) .')';
    $where_args = array_merge($where_args, $projects);
  }

  // Restrict to the status list, if set.
  if ($status_list = pift_status_list()) {
    $where .= ' AND pi.sid IN ('. implode(',', array_fill(0, count($status_list), '%d')) .')';
    $where_args = array_merge($where_args, $status_list);
  }

  // Add in release info if it exists.
  if (module_exists('project_release')) {
    $select = ', prn.version, prn.tag';
    $join = ' LEFT JOIN {project_release_nodes} prn ON pi.rid = prn.nid';
    // Restrict to release tag, if set.
    if ($release_tag_regex = variable_get('pift_release_tag_regex', '')) {
      $where .= " AND prn.tag REGEXP '%s'";
      $where_args[] = $release_tag_regex;
    }
  }
  else {
    $select = '';
    $join = '';
  }

  // Assemble final args.
  $args = array_merge($sub_query_args, $where_args, $sub_query_args, $where_args);

  // Pull issue files that need to be sent for testing.  These include:
  // 1. All files marked as untested that have exceeded the resend period.
  //    since their last send (includes newly added files).
  // 2. All files that have been marked for re-testing.
  // This is one god awful query...  :(
  $files = db_query_range("(SELECT pd.ftid, pd.cid, n.nid AS issue_id, n.title AS issue_title, n.uid, pi.pid, pi.rid, f.filepath AS patch_url, p.title AS project, u.name AS submitter$select FROM {pift_data} pd INNER JOIN {project_issues} pi ON pd.nid = pi.nid INNER JOIN {files} f ON pd.fid = f.fid INNER JOIN {node} n ON n.nid = pi.nid INNER JOIN {node} p ON p.nid = pi.pid INNER JOIN {users} u ON u.uid = pd.uid$join WHERE ((pd.status = %d AND pd.timestamp < %d) OR (pd.status = %d AND pd.timestamp < %d)) AND pd.cid = %d$where) UNION (SELECT pd.ftid, pd.cid, n.nid AS issue_id, n.title AS issue_title, n.uid, pi.pid, pi.rid, f.filepath AS patch_url, p.title AS project, u.name AS submitter$select FROM {pift_data} pd INNER JOIN {project_issues} pi ON pd.nid = pi.nid INNER JOIN {files} f ON pd.fid = f.fid INNER JOIN {node} n ON n.nid = pi.nid INNER JOIN {node} p ON p.nid = pi.pid INNER JOIN {users} u ON u.uid = pd.uid$join WHERE ((pd.status = %d AND pd.timestamp < %d) OR (pd.status = %d AND pd.timestamp < %d)) AND pd.cid <> %d$where) ORDER BY ftid", $args, 0, PIFT_SEND_LIMIT);

  // Set up the batching variables.
  $bid = 0;
  $batches = array();
  $batch_total = 0;
  $sent_files = array();

  // Send new data if there is any.
  while ($file = db_fetch_array($files)) {
    if (is_readable($file['patch_url'])) {
      // Start the next batch if the current one is full.
      if ($batch_total >= PIFT_BATCH_SIZE) {
        $bid++;
        $batch_total = 0;
      }
      // Convert filepath to absolute URL.
      $file['patch_url'] = url($file['patch_url'], array('absolute' => TRUE));
      // Add the file to the current batch.
      $batches[$bid][] = $file;
      // Add to the list of files that will be sent with this batch.
      $sent_files[$bid][] = $file['ftid'];
      // Increment both the current batch size, and the total size.
      $batch_total++;
    }
    // File is not readable, mark as such.
    else {
      db_query("UPDATE {pift_data} SET status = %d WHERE ftid = %d", PIFT_NOT_READABLE, $file['ftid']);
    }
  }

  // Nothing to do.
  if (empty($batches)) {
    return;
  }

  $sent = array();

  // Get the next server in the round robin cycle.
  $s = variable_get('pift_next_test_server', 1);

  $server_key = pift_server_key();

  // Send batches, round robin.
  foreach ($batches as $bid => $batch) {
    // We're at the end of the server line, start over.
    if ($s > $i) {
      $s = 1;
    }
    // Send batch.
    $result = xmlrpc($servers[$s]['xmlrpc_url'], 'pifr.batch.queue', $server_key, $batch);

    // List of successful sends.
    if ($result == PIFR_RESPONSE_ACCEPTED) {
      // Update the last sent timestamp for this batch of files.
      db_query("UPDATE {pift_data} SET timestamp = %d WHERE ftid IN (". implode(', ', $sent_files[$bid]) .")", time());
      $sent[] = check_plain($servers[$s]['xmlrpc_url']) .' -- '. format_plural(count($sent_files[$bid]), '1 file sent', '@count files sent');
    }

    // Failed XML-RPC call.
    elseif ($result === FALSE) {
      pift_xmlrpc_error_handler($servers[$s]);
    }
    // Failed test server validation.
    else {
      pift_xmlrpc_error_handler($servers[$s], $result);
    }

    // Next server in line.
    $s++;
    variable_set('pift_next_test_server', $s);
  }

  // Record successful sends.
  if (!empty($sent)) {
    watchdog('project_remote', 'Project issue data sent successfully to the following servers:!servers', array('!servers' => theme('item_list', $sent)));
  }
}
