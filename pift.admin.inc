<?php
// $Id$
/**
 * @file
 * Provide admin pages.
 *
 * Copyright 2008-2009 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Callback for the main settings page.
 */
function pift_settings_form() {
  $form = array();

  $form['pift_send_frequency'] = array(
    '#type' => 'select',
    '#title' => t('Send frequency'),
    '#options' => array(
      -1 => t('Disabled'),
      0 => t('Each cron run'),
      60 => t('Hourly'),
      1440 => t('Daily'),
      10080 => t('Weekly'),
      40320 => t('Monthly'),
    ),
    '#default_value' => variable_get('pift_send_frequency', -1),
    '#description' => t("How often new files should be sent to test servers."),
  );

  // Warn about node access and login security issues.
  $form['pift_access_warning'] = array(
    '#prefix' => '<div class="pift-server-warning">',
    '#suffix' => '</div>',
    '#value' => t('IMPORTANT:<p>The file test server does not perform node access checks related to the sending of file testing information. Only trust test servers that you have control over, as they are getting access to node information related to any file test sent, regardless of any node access control you impose on your site.</p><p>Keep all server keys listed below confidential.</p>')
  );

  $form['pift_server_key'] = array(
    '#type' => 'item',
    '#title' => t('Server key'),
    '#value' => pift_server_key(),
    '#description' => t("This key must be entered in the <em>Project issue file review</em> settings on any test server you wish to send files to."),
  );

  $form['pift_sites'] = array(
    '#type' => 'textarea',
    '#title' => t('Test sites'),
    '#default_value' => variable_get('pift_sites', ''),
    '#description' => t("List the login information and servers that will receive files for testing. Each site to receive information must have the <em>Project issue file review</em> module installed, and you must include the remote server's private key. Put one remote site per line, in the following format: <em>key@xmlrpc-url</em>. For example, <em>testserversecretkey@http://example.com/xmlrpc.php</em>"),
  );

  $form['pift_file_description'] = array(
    '#type' => 'textarea',
    '#title' => t('Additional file description'),
    '#default_value' => variable_get('pift_file_description', ''),
    '#description' => t("Add text to the description of project issue file upload fields. Can by used to add clarification of files types, etc."),
  );

  $form['pift_debug_file_testing_link'] = array(
    '#type' => 'checkbox',
    '#title' => t('Include debug file testing links'),
    '#default_value' => variable_get('pift_debug_file_testing_link', ''),
    '#description' => t('If selected, a user message will be displayed after each testable patch is submitted, containing links to manually submit a pass/fail test result for the file.'),
  );

  $form['pift_reset_send_queue'] = array(
    '#type' => 'checkbox',
    '#title' => t('Reset sending queue'),
    '#description' => t('If selected, the sending queue will be cleared, and only files submitted from the time of the reset will be sent.'),
  );

  $form['advanced'] = array(
    '#type' => 'fieldset',
    '#title' => t('Advanced settings'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );

  $form['advanced']['pift_send_limit'] = array(
    '#type' => 'textfield',
    '#title' => t('Send limit'),
    '#description' => t('Limit on how many files to send for each cron run. Default is 100.'),
    '#required' => TRUE,
    '#default_value' => variable_get('pift_send_limit', 100),
    '#size' => 10,
    '#maxlength' => 10,
    '#element_validate' => array('pift_is_numeric_validate'),
  );

  $form['advanced']['pift_batch_size'] = array(
    '#type' => 'textfield',
    '#title' => t('Batch size'),
    '#description' => t('Number of files to send in a single call to a test server. Default is 20.'),
    '#required' => TRUE,
    '#default_value' => variable_get('pift_batch_size', 20),
    '#size' => 10,
    '#maxlength' => 10,
    '#element_validate' => array('pift_is_numeric_validate'),
  );

  $form['advanced']['pift_file_regex'] = array(
    '#type' => 'textfield',
    '#title' => t('File regex'),
    '#description' => t('Regex for determining which file types should be sent for testing. Default is all files ending in <em>.patch</em> or <em>.diff</em>. You must include the full regex, ex. <em>/(\.diff|\.patch)$/</em>'),
    '#required' => TRUE,
    '#default_value' => variable_get('pift_file_regex', '/(\.diff|\.patch)$/'),
  );

  $form['advanced']['pift_resend_time'] = array(
    '#type' => 'textfield',
    '#title' => t('Re-send interval for untested files'),
    '#description' => t('Number of hours to wait before re-sending an untested file to a test server (ie, the test server never sent back the results). Default is 2 hours.'),
    '#required' => TRUE,
    '#default_value' => variable_get('pift_resend_time', 2),
    '#size' => 4,
    '#maxlength' => 4,
    '#element_validate' => array('pift_is_numeric_validate'),
  );

  $form['advanced']['pift_retest_time'] = array(
    '#type' => 'textfield',
    '#title' => t('Re-test interval for previously passed files'),
    '#description' => t('Number of hours to wait before re-testing a previously passed file. Set to -1 to prevent re-testing. Default is 24 hours.'),
    '#required' => TRUE,
    '#default_value' => variable_get('pift_retest_time', 24),
    '#size' => 4,
    '#maxlength' => 4,
    '#element_validate' => array('pift_is_numeric_validate'),
  );

  $form['advanced']['pift_projects'] = array(
    '#type' => 'textarea',
    '#title' => t('Projects to test'),
    '#description' => t("A comma separated list of project ID's (nids) that will be examined for new files. Leave empty for all projects."),
    '#default_value' => variable_get('pift_projects', ''),
    '#element_validate' => array('pift_comma_separated_numeric_validate'),
  );

  $form['advanced']['pift_test_status'] = array(
    '#type' => 'textfield',
    '#title' => t('List of issue statuses to test'),
    '#description' => t("Test files attached to issues that have the following issue statuses (should be a comma separated list of issue sids). Leave empty to test every status."),
    '#default_value' => variable_get('pift_test_status', '8, 14'),
    '#element_validate' => array('pift_comma_separated_numeric_validate'),
  );

  if (module_exists('project_release')) {
    $form['advanced']['pift_release_tag_regex'] = array(
      '#type' => 'textfield',
      '#title' => t('Release tag REGEXP'),
      '#description' => t("Enter a MySQL-compatible REGEXP to restrict file testing by the tag column in {project_release_nodes}, for example, <em>^HEAD$</em> -- leave blank for no release tag filtering. "),
      '#default_value' => variable_get('pift_release_tag_regex', ''),
    );
  }

  $criteria = array(
    t("The file has failed testing."),
    t("The issue the file is attached to is still passing the file testing filters for project, issue status, and release tag."),
    t("The file is the last testable file posted to the issue."),
  );
  $form['advanced']['pift_auto_followup_sid'] = array(
    '#type' => 'textfield',
    '#size' => 4,
    '#maxlength' => 4,
    '#title' => t('Auto followup issue status ID'),
    '#description' => t("Set this to the numeric issue status ID you wish to use for auto followups, or set to 0 to disable auto followups. Auto followups are posted upon return of a file's test results, and may also be triggered if the issue metadata for project, issue status, or release tag is changed.  The auto followup will set the issue's status to the issue status ID listed here, and leave a comment linking to the file result table for the tested file, if the following criteria are met:") . theme('item_list', $criteria),
    '#default_value' => variable_get('pift_auto_followup_sid', 0),
    '#element_validate' => array('pift_is_numeric_validate'),
  );

  $form['#submit'] = array('pift_settings_form_submit');
  return system_settings_form($form);
}

/**
 * Handles extra submission tasks on the admin settings page.
 */
function pift_settings_form_submit($form, &$form_state) {
  // Reset the queue.
  if ($form_state['values']['pift_reset_send_queue']) {
    pift_reset_send_queue();
    drupal_set_message(t('Send queue reset.'));
  }
  // Remove the setting so it's not saved to {variable}.
  unset($form_state['values']['pift_reset_send_queue']);
}
