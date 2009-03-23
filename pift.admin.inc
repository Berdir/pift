<?php
// $Id$
/**
 * @file
 * Provide admin pages.
 *
 * Copyright 2008-2009 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * PIFT settings.
 */
function pift_admin_settings_form() {
  $form = array();

  $form['general'] = array(
    '#type' => 'fieldset',
    '#title' => t('General'),
  );
  $form['general']['pift_frequency'] = array(
    '#type' => 'select',
    '#title' => t('Send frequency'),
    '#description' => t('Frequency at which test requests will be sent.'),
    '#options' => array(
      -1 => t('Disabled'),
      0 => t('Cron run'),
      01 * 60 * 60 => t('Hourly'),
      24 * 60 * 60 => t('Daily'),
    ),
    '#default_value' => PIFT_FREQUENCY,
    '#required' => TRUE,
  );
  $form['general']['pift_key'] = array(
    '#type' => 'textfield',
    '#title' => t('Client key'),
    '#description' => t('The key assigned to client.'),
    '#default_value' => PIFT_KEY,
    '#required' => TRUE,
  );
  $form['general']['pift_server'] = array(
    '#type' => 'textfield',
    '#title' => t('Server URL'),
    '#description' => t('The URL of the test server, include ending forward slash.'),
    '#default_value' => PIFT_SERVER,
    '#required' => TRUE,
  );

  $form['interface'] = array(
    '#type' => 'fieldset',
    '#title' => t('User interface'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );
  $form['interface']['pift_description'] = array(
    '#type' => 'textarea',
    '#title' => t('File description'),
    '#description' => t('Additional text to be displayed on the project issue attachment fieldset.'),
    '#default_value' => PIFT_DESCRIPTION,
  );
  $criteria = array(
    t('The file has failed testing.'),
    t('The issue the file is attached to is still passing the file testing filters for project, issue status, and release tag.'),
    t('The file is the last testable file posted to the issue.'),
  );
  $form['interface']['pift_followup'] = array(
    '#type' => 'select',
    '#title' => t('Followup issue status'),
    '#description' => t('The status to set an issue to when a test result meets any of the following criteria.' . theme('item_list', $criteria)),
    '#default_value' => PIFT_FOLLOWUP,
    '#options' => project_issue_state(),
    '#required' => TRUE,
  );

  $form['criteria'] = array(
    '#type' => 'fieldset',
    '#title' => t('Criteria'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );
  $form['criteria']['pift_regex'] = array(
    '#type' => 'textfield',
    '#title' => t('File regex'),
    '#description' => t('Full regex pattern that a filename must match for the file to be tested.'),
    '#default_value' => PIFT_REGEX,
    '#required' => TRUE,
  );
  $form['criteria']['pift_status'] = array(
    '#type' => 'select',
    '#multiple' => TRUE,
    '#title' => t('Issue statuses'),
    '#description' => t('List of issue status that an issue must be set to in order to test files.'),
    '#default_value' => variable_get('pift_status', array()),
    '#options' => project_issue_state(),
    '#required' => TRUE,
  );
  $form['criteria']['pift_tag'] = array(
    '#type' => 'textfield',
    '#title' => t('Release tag regex'),
    '#description' => t('MySQL compatible regex pattern that a release tag related to the issue must be match for a file to be tested.'),
    '#default_value' => PIFT_TAG,
    '#required' => TRUE,
  );
  $form['criteria']['pift_retest'] = array(
    '#type' => 'select',
    '#title' => t('Re-test interval'),
    '#description' => t('Number of days to wait before re-testing a previously passed file, or -1 to prevent re-testing.'),
    '#default_value' => PIFT_RETEST,
    '#options' => array(
      -1,
      24 * 60 * 60,
      48 * 60 * 60,
      72 * 60 * 60,
      96 * 60 * 60,
    ),
    '#element_validate' => array('pift_is_numeric_validate'),
    '#required' => TRUE,
  );
  $form['criteria']['pift_retest']['#options'] = drupal_map_assoc($form['criteria']['pift_retest']['#options'], 'format_interval');
  $form['criteria']['pift_retest']['#options'][-1] = t('Disabled');

  $form['pift_reset_send_queue'] = array(
    '#type' => 'checkbox',
    '#title' => t('Reset sending queue'),
    '#description' => t('If selected, the sending queue will be cleared, and only files submitted from the time of the reset will be sent.'),
  );

  $form['#submit'] = array('pift_admin_settings_form_submit');
  return system_settings_form($form);
}

/**
 * Validate general settings.
 */
function pift_admin_settings_form_validate($form, &$form_state) {
  if (drupal_strlen($form_state['values']['pift_key']) != 32) {
    form_set_error('client_key', t('Key must be 32 characters long.'));
  }

  if (!$form_state['values']['pift_server'] || !@parse_url($form_state['values']['pift_server'])) {
    form_set_error('pift_server', t('Server URL must be a complete URL, schema included.'));
  }
  else if (drupal_substr($form_state['values']['pift_server'],
                          drupal_strlen($form_state['values']['pift_server']) - 1) != '/') {
    form_set_error('pift_server', t('Server URL must end with a slash.'));
  }
}

/**
 * Handles extra submission tasks on the admin settings page.
 */
function pift_admin_settings_form_submit($form, &$form_state) {
  // Reset the queue.
  if ($form_state['values']['pift_reset_send_queue']) {
    pift_reset_send_queue();
    drupal_set_message(t('Send queue reset.'));
  }
  // Remove the setting so it's not saved to {variable}.
  unset($form_state['values']['pift_reset_send_queue']);
}
