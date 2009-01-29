<?php
// $Id$

/**
 * @file
 * Administrative page for adding files for the Project issue file testing
 * framework to manage.  This is most often used to add existing patches
 * to the testing queue which were present before the module was installed.
 *
 * Place this file in the root of your Drupal installation (ie, the same
 * directory as index.php), point your browser to
 * "http://yoursite/pift_server_add_files.php" and follow the
 * instructions.
 *
 * If you are not logged in as administrator, you will need to modify the access
 * check statement below. Change the TRUE to a FALSE to disable the access
 * check. After finishing the upgrade, be sure to open this file and change the
 * FALSE back to a TRUE!
 */

// Enforce access checking?
$access_check = TRUE;

/**
 * Add issue files.
 */
function pift_server_add_files_update_1() {

  // This determines how many issue files will be processed in each batch run. A reasonable
  // default has been chosen, but you may want to tweak depending on your setup.
  $limit = 200;

  // Multi-part update
  if (!isset($_SESSION['pift_server_add_files_update_1'])) {
    $_SESSION['pift_server_add_files_update_1'] = 0;
    $_SESSION['pift_server_add_files_update_1_max'] = db_result(db_query("SELECT COUNT(*) FROM {node} n INNER JOIN {upload} u ON n.nid = u.nid INNER JOIN {files} f ON u.fid = f.fid WHERE n.type = 'project_issue' AND f.fid >= %d AND f.fid <= %d AND f.filepath <> ''", $_SESSION['first_issue_fid'], $_SESSION['last_issue_fid']));
  }

  $ret = array();

  // Pull all new issue data. Limit the results to 5 times the PIFT_SEND_LIMIT for consistency.
  $issue_files = db_query_range("SELECT n.nid, n.uid, f.fid, f.filename, f.filepath FROM {node} n INNER JOIN {upload} u ON n.nid = u.nid INNER JOIN {files} f ON u.fid = f.fid WHERE n.type = 'project_issue' AND f.fid >= %d AND f.fid <= %d AND f.filepath <> '' ORDER BY n.nid, f.fid", $_SESSION['first_issue_fid'], $_SESSION['last_issue_fid'], $_SESSION['pift_server_add_files_update_1'], $limit);

  while ($file = db_fetch_object($issue_files)) {

    // Put the file data into the send queue.
    if (preg_match($_SESSION['file_regex'], $file->filename) && file_exists($file->filepath)) {
      $ret[] = pift_update_sql("INSERT INTO {pift_data} (fid, nid, cid, uid, display_data, status, timestamp) VALUES (%d, %d, %d, %d, '%s', %d, %d)", $file->fid, $file->nid, 0, $file->uid, '', PIFT_UNTESTED, 0);
    }

    $_SESSION['pift_server_add_files_update_1']++;
  }

  if ($_SESSION['pift_server_add_files_update_1'] >= $_SESSION['pift_server_add_files_update_1_max']) {
    unset($_SESSION['pift_server_add_files_update_1']);
    unset($_SESSION['pift_server_add_files_update_1_max']);
    return $ret;
  }

  $ret['#finished'] = $_SESSION['pift_server_add_files_update_1'] / $_SESSION['pift_server_add_files_update_1_max'];
  return $ret;
}

/**
 * Add followup files.
 */
function pift_server_add_files_update_2() {

  // This determines how many issue files will be processed in each batch run. A reasonable
  // default has been chosen, but you may want to tweak depending on your setup.
  $limit = 200;

  // Multi-part update
  if (!isset($_SESSION['pift_server_add_files_update_2'])) {
    $_SESSION['pift_server_add_files_update_2'] = 0;
    $_SESSION['pift_server_add_files_update_2_max'] = db_result(db_query("SELECT COUNT(*) FROM {node} n INNER JOIN {comments} c ON n.nid = c.nid INNER JOIN {comment_upload} cu ON c.cid = cu.cid INNER JOIN {files} f ON cu.fid = f.fid WHERE n.type = 'project_issue' AND f.fid >= %d AND f.fid <= %d AND f.filepath <> ''", $_SESSION['first_issue_fid'], $_SESSION['last_issue_fid']));
  }

  $ret = array();

  // Pull all new followup file data. Limit the results to 5 times the PIFT_SEND_LIMIT for consistency.
  $followup_files = db_query_range("SELECT n.nid, c.cid, c.uid, f.fid, f.filename, f.filepath FROM {node} n INNER JOIN {comments} c ON n.nid = c.nid INNER JOIN {comment_upload} cu ON c.cid = cu.cid INNER JOIN {files} f ON cu.fid = f.fid WHERE n.type = 'project_issue' AND f.fid >= %d AND f.fid <= %d AND f.filepath <> '' ORDER BY c.cid, f.fid", $_SESSION['first_issue_fid'], $_SESSION['last_issue_fid'], $_SESSION['pift_server_add_files_update_2'], $limit);

  while ($file = db_fetch_object($followup_files)) {

    // Put the file data into the send queue.
    if (preg_match($_SESSION['file_regex'], $file->filename) && file_exists($file->filepath)) {
      $ret[] = pift_update_sql("INSERT INTO {pift_data} (fid, nid, cid, uid, display_data, status, timestamp) VALUES (%d, %d, %d, %d, '%s', %d, %d)", $file->fid, $file->nid, $file->cid, $file->uid, '', PIFT_UNTESTED, 0);
    }

    $_SESSION['pift_server_add_files_update_2']++;
  }

  if ($_SESSION['pift_server_add_files_update_2'] >= $_SESSION['pift_server_add_files_update_2_max']) {
    unset($_SESSION['pift_server_add_files_update_2']);
    unset($_SESSION['pift_server_add_files_update_2_max']);
    unset($_SESSION['first_issue_fid']);
    unset($_SESSION['last_issue_fid']);
    unset($_SESSION['file_regex']);
    return $ret;
  }

  $ret['#finished'] = $_SESSION['pift_server_add_files_update_2'] / $_SESSION['pift_server_add_files_update_2_max'];
  return $ret;
}

function pift_update_sql() {
  $args = func_get_args();
  $sql = array_shift($args);
  $result = db_query($sql, $args);
  return array('success' => $result !== FALSE, 'query' => check_plain($sql));
}

/**
 * Perform one update and store the results which will later be displayed on
 * the finished page.
 *
 * @param $module
 *   The module whose update will be run.
 * @param $number
 *   The update number to run.
 *
 * @return
 *   TRUE if the update was finished. Otherwise, FALSE.
 */
function update_data($module, $number) {

  $function = "pift_server_add_files_update_$number";
  $ret = $function();

  // Assume the update finished unless the update results indicate otherwise.
  $finished = 1;
  if (isset($ret['#finished'])) {
    $finished = $ret['#finished'];
    unset($ret['#finished']);
  }

  // Save the query and results for display by update_finished_page().
  if (!isset($_SESSION['update_results'])) {
    $_SESSION['update_results'] = array();
  }
  if (!isset($_SESSION['update_results'][$module])) {
    $_SESSION['update_results'][$module] = array();
  }
  if (!isset($_SESSION['update_results'][$module][$number])) {
    $_SESSION['update_results'][$module][$number] = array();
  }
  $_SESSION['update_results'][$module][$number] = array_merge($_SESSION['update_results'][$module][$number], $ret);

  return $finished;
}

function update_selection_page() {
  $output = '';
  $output .= '<p>Click Update to start the update process.</p>';

  drupal_set_title('Project issue file test add file update.');
  // Use custom update.js.
  drupal_add_js(update_js(), 'inline');
  $output .= drupal_get_form('update_script_selection_form');

  return $output;
}

function update_script_selection_form(&$form_state) {
  $form = array();

  // Issue file settings.
  $form['first_issue_fid'] = array(
    '#type' => 'textfield',
    '#title' => 'First issue file ID',
    '#size' => 15,
    '#description' => "The file ID for the first issue attachment you wish to add.  All issue file ID's of this value and greater will be added to the testing queue. Leave blank to get all issue attachments from the beginning.",
  );
  $form['last_issue_fid'] = array(
    '#type' => 'textfield',
    '#title' => 'Last issue file ID',
    '#size' => 15,
    '#description' => "The file ID for the last issue attachment you wish to add.  All issue file ID's of this value and less will be added to the testing queue. Leave blank to get all issue attachments to the end.",
  );

  // Matching file regex.
  $form['file_regex'] = array(
    '#type' => 'textfield',
    '#title' => 'File regex',
    '#size' => 50,
    '#default_value' => '/(\.diff|\.patch)$/',
    '#description' => "A regular expression to match filenames against -- only filenames matching the regular expression will be added for testing, for example <em>/(\.diff|\.patch)$/</em> -- leave blank for no filename filtering.",
  );

  $form['has_js'] = array(
    '#type' => 'hidden',
    '#default_value' => FALSE,
    '#attributes' => array('id' => 'edit-has_js'),
  );
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => 'Update',
  );
  return $form;
}

function update_update_page() {
  // Set the installed version so updates start at the correct place.
  $_SESSION['update_remaining'][] = array('module' => 'pift_server', 'version' => 1);
  $_SESSION['update_remaining'][] = array('module' => 'pift_server', 'version' => 2);

  // Set the default repo.
  $_SESSION['first_issue_fid'] = $_POST['first_issue_fid'] ? (int) $_POST['first_issue_fid'] : 0;
  $_SESSION['last_issue_fid'] = $_POST['last_issue_fid'] ? (int) $_POST['last_issue_fid'] : db_result(db_query("SELECT MAX(fid) FROM {files}"));
  $_SESSION['file_regex'] = $_POST['file_regex'] ? $_POST['file_regex'] : '/.*/';

  // Keep track of total number of updates
  if (isset($_SESSION['update_remaining'])) {
    $_SESSION['update_total'] = count($_SESSION['update_remaining']);
  }

  if ($_POST['has_js']) {
    return update_progress_page();
  }
  else {
    return update_progress_page_nojs();
  }
}

function update_progress_page() {
  // Prevent browser from using cached drupal.js or update.js
  drupal_add_js('misc/progress.js', 'core', 'header', FALSE, TRUE);
  drupal_add_js(update_js(), 'inline');

  drupal_set_title('Updating');
  $output = '<div id="progress"></div>';
  $output .= '<p id="wait">Please wait while your site is being updated.</p>';
  return $output;
}

/**
 * Can't include misc/update.js, because it makes a direct call to update.php.
 *
 * @return unknown
 */
function update_js() {
  return "
  if (Drupal.jsEnabled) {
    $(document).ready(function() {
      $('#edit-has-js').each(function() { this.value = 1; });
      $('#progress').each(function () {
        var holder = this;

        // Success: redirect to the summary.
        var updateCallback = function (progress, status, pb) {
          if (progress == 100) {
            pb.stopMonitoring();
            window.location = window.location.href.split('op=')[0] +'op=finished';
          }
        }

        // Failure: point out error message and provide link to the summary.
        var errorCallback = function (pb) {
          var div = document.createElement('p');
          div.className = 'error';
          $(div).html('An unrecoverable error has occured. You can find the error message below. It is advised to copy it to the clipboard for reference. Please continue to the <a href=\"pift_server_add_files.php?op=error\">update summary</a>');
          $(holder).prepend(div);
          $('#wait').hide();
        }

        var progress = new Drupal.progressBar('updateprogress', updateCallback, \"POST\", errorCallback);
        progress.setProgress(-1, 'Starting updates');
        $(holder).append(progress.element);
        progress.startMonitoring('pift_server_add_files.php?op=do_update', 0);
      });
    });
  }
  ";
}

/**
 * Perform updates for one second or until finished.
 *
 * @return
 *   An array indicating the status after doing updates. The first element is
 *   the overall percentage finished. The second element is a status message.
 */
function update_do_updates() {
  while (isset($_SESSION['update_remaining']) && ($update = reset($_SESSION['update_remaining']))) {
    $update_finished = update_data($update['module'], $update['version']);
    if ($update_finished == 1) {
      // Dequeue the completed update.
      unset($_SESSION['update_remaining'][key($_SESSION['update_remaining'])]);
      $update_finished = 0; // Make sure this step isn't counted double
    }
    if (timer_read('page') > 1000) {
      break;
    }
  }

  if ($_SESSION['update_total']) {
    $percentage = floor(($_SESSION['update_total'] - count($_SESSION['update_remaining']) + $update_finished) / $_SESSION['update_total'] * 100);
  }
  else {
    $percentage = 100;
  }

  // When no updates remain, clear the caches in case the data has been updated.
  if (!isset($update['module'])) {
    cache_clear_all('*', 'cache', TRUE);
    cache_clear_all('*', 'cache_page', TRUE);
    cache_clear_all('*', 'cache_menu', TRUE);
    cache_clear_all('*', 'cache_filter', TRUE);
    drupal_clear_css_cache();
  }

  return array($percentage, isset($update['module']) ? 'Updating '. $update['module'] .' module' : 'Updating complete');
}

/**
 * Perform updates for the JS version and return progress.
 */
function update_do_update_page() {
  global $conf;

  // HTTP Post required
  if ($_SERVER['REQUEST_METHOD'] != 'POST') {
    drupal_set_message('HTTP Post is required.', 'error');
    drupal_set_title('Error');
    return '';
  }

  // Error handling: if PHP dies, the output will fail to parse as JSON, and
  // the Javascript will tell the user to continue to the op=error page.
  list($percentage, $message) = update_do_updates();
  print drupal_to_js(array('status' => TRUE, 'percentage' => $percentage, 'message' => $message));
}

/**
 * Perform updates for the non-JS version and return the status page.
 */
function update_progress_page_nojs() {
  drupal_set_title('Updating');

  $new_op = 'do_update_nojs';
  if ($_SERVER['REQUEST_METHOD'] == 'GET') {
    // Error handling: if PHP dies, it will output whatever is in the output
    // buffer, followed by the error message.
    ob_start();
    $fallback = '<p class="error">An unrecoverable error has occurred. You can find the error message below. It is advised to copy it to the clipboard for reference. Please continue to the <a href="pift_server_add_files.php?op=error">update summary</a>.</p>';
    print theme('maintenance_page', $fallback, FALSE, TRUE);

    list($percentage, $message) = update_do_updates();
    if ($percentage == 100) {
      $new_op = 'finished';
    }

    // Updates successful; remove fallback
    ob_end_clean();
  }
  else {
    // Abort the update if the necessary modules aren't installed.
    if (!module_exists('pift_server') || !module_exists('project') || !module_exists('project_issue')) {
      print update_finished_page(FALSE);
      return NULL;
    }

    // This is the first page so return some output immediately.
    $percentage = 0;
    $message = 'Starting updates';
  }

  drupal_set_html_head('<meta http-equiv="Refresh" content="0; URL=pift_server_add_files.php?op='. $new_op .'">');
  $output = theme('progress_bar', $percentage, $message);
  $output .= '<p>Updating your site will take a few seconds.</p>';

  // Note: do not output drupal_set_message()s until the summary page.
  print theme('maintenance_page', $output, FALSE);
  return NULL;
}

function update_finished_page($success) {
  drupal_set_title('Project issue file test add file update.');
  // NOTE: we can't use l() here because the URL would point to 'update.php?q=admin'.
  $links[] = '<a href="'. base_path() .'">Main page</a>';
  $links[] = '<a href="'. base_path() .'?q=admin">Administration pages</a>';

  // Report end result
  if ($success) {
    $output = '<p>Updates were attempted. If you see no failures below, you should remove pift_server_add_files.php from your Drupal root directory. Otherwise, you may need to update your database manually. All errors have been <a href="index.php?q=admin/reports/watchdog">logged</a>.</p>';
  }
  else {
    $output = '<p class="error">The update process was aborted prematurely. All other errors have been <a href="index.php?q=admin/reports/watchdog">logged</a>. You may need to check the <code>watchdog</code> database table manually.</p>';
    $output .= '<p class="error">This has most likely occurred because the Project issue file test module or the Project/Project issue modules are not <a href="index.php?q=admin/build/modules">properly installed</a>.</p>';
  }

  $output .= theme('item_list', $links);

  if ($success) {
    $output .= "<h4>Some things to take care of now:</h4>\n";
    $output .= "<ul>\n";
    $output .= "<li>Visit the <a href=\"index.php?q=admin/project/project-issue-file-test-server\">Project issue file test server administration page</a>, and configure.</li>\n";
    $output .= "<li>Install the <a href=\"http://drupal.org/project/project_issue_file_review\">Project issue file review</a> module and configure it to work with your server installation.</li>\n";
    $output .= "</ul>\n";
  }

  // Output a list of queries executed
  if (!empty($_SESSION['update_results'])) {
    $output .= '<div id="update-results">';
    $output .= '<h2>The following queries were executed</h2>';
    foreach ($_SESSION['update_results'] as $module => $updates) {
      $output .= '<h3>'. $module .' module</h3>';
      foreach ($updates as $number => $queries) {
        $output .= '<h4>Update #'. $number .'</h4>';
        $output .= '<ul>';
        foreach ($queries as $query) {
          if ($query['success']) {
            $output .= '<li class="success">'. $query['query'] .'</li>';
          }
          else {
            $output .= '<li class="failure"><strong>Failed:</strong> '. $query['query'] .'</li>';
          }
        }
        if (!count($queries)) {
          $output .= '<li class="none">No queries</li>';
        }
        $output .= '</ul>';
      }
    }
    $output .= '</div>';
    unset($_SESSION['update_results']);
  }

  return $output;
}

function update_info_page() {
  drupal_set_title('Project issue file test add file update.');
  $output = "<ol>\n";
  $output .= "<li>Use this script to add any Project issue file attachments that were made before the file testing module was installed.</li>";
  $output .= "<li>Before doing anything, backup your database. This process will change your database and its values.</li>\n";
  $output .= "<li>Make sure the Project issue file test, Project, and Project issue modules are <a href=\"index.php?q=admin/build/modules\">properly installed</a>.</li>\n";
  $output .= "<li>Make sure this file is placed in the root of your Drupal installation (the same directory that index.php is in) and <a href=\"pift_server_add_files.php?op=selection\">run the database upgrade script</a>. <strong>Don't upgrade your database twice as it will cause problems!</strong></li>\n";
  $output .= "</ol>";

  return $output;
}

function update_access_denied_page() {
  drupal_set_title('Access denied');
  return '<p>Access denied. You are not authorized to access this page. Please log in as the admin user (the first user you created). If you cannot log in, you will have to edit <code>pift_server_add_files.php</code> to bypass this access check. To do this:</p>
<ol>
 <li>With a text editor find the pift_server_add_files.php file on your system. It should be in the main Drupal directory that you installed all the files into.</li>
 <li>There is a line near top of pift_server_add_files.php that says <code>$access_check = TRUE;</code>. Change it to <code>$access_check = FALSE;</code>.</li>
 <li>As soon as the update is done, you should remove pift_server_add_files.php from your main installation directory.</li>
</ol>';
}

include_once './includes/bootstrap.inc';

drupal_bootstrap(DRUPAL_BOOTSTRAP_FULL);
drupal_maintenance_theme();

// Access check:
if (($access_check == FALSE) || ($user->uid == 1)) {

  $op = isset($_REQUEST['op']) ? $_REQUEST['op'] : '';
  switch ($op) {
    case 'Update':
      $output = update_update_page();
      break;

    case 'finished':
      $output = update_finished_page(TRUE);
      break;

    case 'error':
      $output = update_finished_page(FALSE);
      break;

    case 'do_update':
      $output = update_do_update_page();
      break;

    case 'do_update_nojs':
      $output = update_progress_page_nojs();
      break;

    case 'selection':
      $output = update_selection_page();
      break;

    default:
      $output = update_info_page();
      break;
  }
}
else {
  $output = update_access_denied_page();
}

if (isset($output)) {
  print theme('maintenance_page', $output);
}
