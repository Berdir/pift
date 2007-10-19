<?php
// $Id$
// $Name$

/**
 * Simple test testing function.  Files fail if the word 'fail'
 * is included in the file, otherwise they pass.
 *
 * $file is an associative array representing a file to test, with the following
 * key/value pairs:
 *
 *     'ftid'          => The file test ID that was originally passed from
 *                       the test server for the file in question.
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
 *     'submitter'     => Drupal user submitting the patch.
 *
 *     'patch_url'     => The absolute URL for the file.
 *
 *     'project_server => XML-RPC URL of project server requesting the
 *                        test results.
 */
$test_result['ftid'] = $file['ftid'];
$project_server = $file['project_server'];

// Read in file.
$fd = fopen($file['patch_url'], 'r');
$file['data'] = '';
while (!feof($fd)) {
    $file['data'] .= fread($fd, 1024);
}
fclose($fd);

// Fails test if it contains the word 'fail'.
if (strpos($file['data'], 'fail') !== FALSE) {
  $test_result['display_data'] = "{$file['project']}, {$file['version']}, by {$file['submitter']} -- contains the word 'fail'";
  $test_result['status'] = PIFT_FAILED;
}
else {
  $test_result['display_data'] = "{$file['project']}, {$file['version']}, by {$file['submitter']} -- all syntax good";
  $test_result['status'] = PIFT_PASSED;
}

pift_client_send_test_results($project_server, array($test_result));

