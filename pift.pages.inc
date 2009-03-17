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

/**
 * Confirm re-test.
 *
 * @param integer Test ID to re-test.
 */
function pift_pages_retest_confirm_form(&$form_state, $test_id) {
  $test = pift_test_get($test_id);

  if ($test) {
    $form = array();
    $form['#test_id'] = array(
      '#type' => 'value',
      '#value' => $test_id,
    );

    $title = $test['fid'] ? $test['filename'] : project_release_get_version((object) $test);
    return confirm_form(
      $form,
      t('Are you sure you want to request that %title be re-tested?', array('%title' => $title)),
      '', // TODO
      t('Once requested the test will be re-queued and results will be updated once the re-test has finished.'),
      t('Re-test'),
      t('Cancel')
    );
  }
  else {
    drupal_set_message(t('Invalid test.'), 'error');
  }
}

/**
 * Theme attachments into a table with test results.
 *
 * @param array $files List of file attachments.
 * @return string HTML output.
 */
function theme_pift_attachments(array $files) {
  $header = array(t('Attachment'), t('Size'), t('Test results'));
  $rows = array();
  foreach ($files as $file) {
    $row = array();
    $row[] = l($file['filename'], $file['filepath']);
    $row[] = format_size($file['filesize']);
    if ($file['test_id']) {
      $row[] = $file['status'] > PIFT_QUEUE ? check_plain($file['message']) : '<em>' . t('Queued for testing.') . '</em>';
      if ($file['status'] == PIFT_PASS) {
        $class = 'pift-pass';
      }
      elseif ($file['status'] == PIFT_FAIL) {
        $class = 'pift-fail';
      }
      elseif ($file['status'] == PIFT_RETEST) {
        $class = 'pift-retest';
      }
      $rows[] = array('class' => 'pift-passed', 'data' => $row);
    }
    else {
      $row[] = '<em>' . t('Ignored.') . '</em>';
      $rows[] = $row;
    }
  }
  return theme('table', $header, $rows);
}
