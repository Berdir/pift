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
  $form['interface']['pift_followup_fail'] = array(
    '#type' => 'select',
    '#title' => t('Followup issue status - fail'),
    '#description' => t('The status to set an issue to when a test result meets all of the following criteria.' . theme('item_list', $criteria)),
    '#default_value' => PIFT_FOLLOWUP_FAIL,
    '#options' => project_issue_state(),
    '#required' => TRUE,
  );
  $form['interface']['pift_followup_retest'] = array(
    '#type' => 'select',
    '#title' => t('Followup issue status - retest'),
    '#description' => t('The status to set an issue to when someone requests a re-test and the issue currently does not fit the test criteria.'),
    '#default_value' => PIFT_FOLLOWUP_RETEST,
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
  $form['criteria']['pift_core'] = array(
    '#type' => 'checkboxes',
    '#title' => t('Core branches'),
    '#description' => t('Drupal core branches that are to be tested. Issues pertaining to them will have files tested.'),
    '#options' => array(
      6 => t('6.x'),
      7 => t('7.x'),
    ),
    '#default_value' => variable_get('pift_core', array()),
  );

  $form['advanced'] = array(
    '#type' => 'fieldset',
    '#title' => t('Advanced'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );
  $project = node_load(PIFT_PID);
  $form['advanced']['pift_pid'] = array(
    '#type' => 'textfield',
    '#title' => t('Core'),
    '#description' => t('Project that contains the core code required by modules to function.'),
    '#autocomplete_path' => 'project/autocomplete/issue/project',
    '#default_value' => ($project ? $project->title : ''),
    '#required' => TRUE,
  );
  $form['advanced']['pift_retest'] = array(
    '#type' => 'select',
    '#title' => t('Re-test interval'),
    '#description' => t('Number of days to wait before re-testing a previously passed file, or -1 to prevent re-testing.'),
    '#options' => array(
      -1,
      24 * 60 * 60,
      48 * 60 * 60,
      72 * 60 * 60,
      96 * 60 * 60,
    ),
    '#default_value' => PIFT_RETEST,
    '#element_validate' => array('pift_is_numeric_validate'),
    '#required' => TRUE,
  );
  $form['advanced']['pift_retest']['#options'] = drupal_map_assoc($form['advanced']['pift_retest']['#options'], 'format_interval');
  $form['advanced']['pift_retest']['#options'][-1] = t('Disabled');

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
 * Query pid using project title.
 */
function pift_admin_settings_form_submit($form, &$form_state) {
  $form_state['values']['pift_pid'] = db_result(
    db_query("SELECT nid
              FROM {node}
              WHERE title = '%s'
              AND type = '%s'", $form_state['values']['pift_pid'], 'project_project')
  );
}
