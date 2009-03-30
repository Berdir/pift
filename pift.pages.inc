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
    $form['test'] = array(
      '#type' => 'value',
      '#value' => array(
        'test_id' => $test_id,
        'fid' => $test['fid'],
        'nid' => ($test['fid'] ? $test['nid'] : $test['rid']),
        'cid' => ($test['cid'] ? $test['cid'] : 0),
        'status' => $test['status'],
        'title' => ($test['fid'] ? $test['filename'] : project_release_get_version((object) $test))
      ),
    );
    $form['#redirect'] = 'node/' . $form['test']['#value']['nid'];

    return confirm_form(
      $form,
      t('Are you sure you want to request that %title be re-tested?', array('%title' => $form['test']['#value']['title'])),
      $form['#redirect'],
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
 * Re-queue test if it still meets are criteria.
 */
function pift_pages_retest_confirm_form_submit($form, &$form_state) {
  $test = $form_state['values']['test'];

  if ($test['status'] > PIFT_STATUS_SENT) {
    if ($test['fid'] && !pift_test_check_criteria_issue(node_load($test['nid']))) {
      project_issue_add_auto_followup(array(
        'nid' => $test['nid'],
        'sid' => PIFT_FOLLOWUP_RETEST,
        'comment' => theme('pift_auto_followup', 'retest', $test['nid'], $test['cid']),
      ));
    }

    db_query('UPDATE {pift_test}
              SET status = %d
              WHERE test_id = %d', PIFT_STATUS_QUEUE, $test['test_id']);

    if (db_affected_rows()) {
      drupal_set_message(t('%title has been submitted for re-testing. Please be patient while you wait for results.',
                           array('%title' => $test['title'])));
    }
    else {
      drupal_set_message(t('Invalid test.'), 'error');
    }
  }
  else {
    drupal_set_message(t('%title is not currently eligible for re-testing.',
                         array('%title' => $test['title'])), 'error');
  }
}

/**
 * Theme attachments into a table with test results.
 *
 * @param array $files List of file attachments.
 * @return string HTML output.
 */
function theme_pift_attachments(array $files) {
  $header = array(t('Attachment'), t('Size'), t('Test results'), t('Operations'));
  $rows = array();
  foreach ($files as $file) {
    $row = array();

    $row[] = l($file['filename'], $file['filepath']);
    $row[] = format_size($file['filesize']);

    if ($file['test_id'] !== NULL) {
      list($row[], $class) = pift_attachment_process($file['status'], $file['message']);
    }
    else {
      $row[] = '<em>' . t('Ignored') . '</em>';
      $class = '';
    }

    // Add valid operations.
    $operations = array();
    if ($file['status'] > PIFT_STATUS_SENT) {
      $operations[] = l(t('View details'), PIFT_SERVER . 'pifr/test/' . $file['test_id']);
      $operations[] = l(t('Re-test'), 'pift/retest/' . $file['test_id']);
    }
//    TODO Implement later.
//    if ($file['test_id']) {
//      $operations[] = l(t('Try now'), 'pift/trynow/' . $file['test_id']);
//    }
    $row[] = ($operations ? implode(' | ', $operations) : '<em>' . t('None')) . '</em>';

    $rows[] = array('class' => $class, 'data' => $row);
  }
  return theme('table', $header, $rows);
}

/**
 * Process the specified attachment information and return the valid cell
 * contents and row CSS class.
 *
 * @param integer $status Test status, PIFT_STATUS_*.
 * @param string $message Message result of testing.
 * @return array Cell value and CSS class.
 */
function pift_attachment_process($status, $message) {
  $class = '';
  if ($status == PIFT_STATUS_QUEUE && $message) {
    $cell = '<em>' . t('Queued for re-testing') . '</em>';
    $class = 'pift-retest';
  }
  else if ($status == PIFT_STATUS_QUEUE) {
    $cell = '<em>' . t('Queued for testing') . '</em>';
  }
  elseif ($status == PIFT_STATUS_SENT) {
    $cell = '<em>' . t('Test request sent') . '</em>';
  }
  elseif ($status > PIFT_STATUS_SENT) {
    $cell = check_plain($message);
    $class = ($status == PIFT_STATUS_PASS ? 'pift-pass' : 'pift-fail');
  }
  return array($cell, $class);
}
