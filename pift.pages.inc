<?php
// $Id$
/**
 * @file
 * Provide general pages and UI functions.
 *
 * Copyright 2008-2009 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Add additional description to attachment form.
 */
function pift_pages_description_add(&$form, $form_state, $form_id) {
  global $user;
  if ($form_id == 'comment_form') {
    $node = node_load($form['nid']['#value']);
    $nid = $node->project_issue['pid'];
  }
  elseif ($form_id == 'project_issue_node_form') {
    $nid = $form['project_info']['pid']['#value'];
  }

  if (isset($form['attachments']['wrapper']['new']['upload']) && pift_project_enabled($nid)) {
    $description = filter_xss_admin(PIFT_DESCRIPTION);
    $form['attachments']['wrapper']['new']['upload']['#description'] .= $description;
  }
}

/**
 * Add testing setting.
 */
function pift_pages_project_issue_settings(&$form, $form_state) {
  $form['issue']['#weight'] = -10;
  $form['issue']['#email'] = -8;
  $form['testing'] = array(
    '#type' => 'fieldset',
    '#title' => t('Testing'),
    '#collapsible' => TRUE,
    '#weight' => -9,
  );
  $form['testing']['pift_enable'] = array(
    '#type' => 'checkbox',
    '#title' => t('Enable automated testing'),
    '#description' => t('Enable automated testing of 7.x releases and patches.'),
    '#default_value' => pift_project_enabled($form['nid']['#value']),
  );
  $form['#submit'][] = 'pift_pages_project_issue_settings_submit';
}

/**
 * Store project testing preferences.
 */
function pift_pages_project_issue_settings_submit($form, &$form_state) {
  $enabled = pift_project_enabled($form_state['values']['nid']);
  dpm($enabled ? 'true' : 'false');
  if ($form_state['values']['pift_enable'] && !$enabled) {
    db_query('INSERT INTO {pift_project}
              VALUES (%d)', $form_state['values']['nid']);
  }
  else if (!$form_state['values']['pift_enable'] && $enabled) {
    db_query('DELETE FROM {pift_project}
              WHERE project_nid = %d', $form_state['values']['nid']);
  }
}
