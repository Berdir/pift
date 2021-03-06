<?php
// $Id$

/**
 * @file
 * Provide general pages and UI functions.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Add additional description to attachment form.
 */
function pift_pages_description_add(&$form, $form_state, $form_id) {
  global $user;

  // Add description on uploads if it is set.
  if (PIFT_DESCRIPTION) {
    // Detect project issue node ID.
    $nid = NULL;
    if ($form_id == 'comment_form' && isset($form['original_issue'])) {
      $node = node_load($form['nid']['#value']);
      $nid = $node->project_issue['pid'];
    }
    elseif ($form_id == 'project_issue_node_form') {
      $nid = $form['project_info']['pid']['#value'];
    }

    if (isset($form['attachments']['wrapper']['new']['upload']) && $nid && pift_project_enabled($nid)) {
      $description = filter_xss_admin(PIFT_DESCRIPTION);
      $form['attachments']['wrapper']['new']['upload']['#description'] .= $description;
    }
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
    '#access' => user_access('pift enable project testing'),
  );

  // Build an array of core capatible version strings (ie 6.x, 7.x, etc).
  $api_versions = pift_core_api_versions();
  $versions = array();
  foreach ($api_versions as $api_version) {
    $term = taxonomy_get_term($api_version);
    $versions[] = $term->name;
  }
  $form['testing']['pift_enable'] = array(
    '#type' => 'checkbox',
    '#title' => t('Enable automated testing'),
    '#description' => t('Enable automated testing of @versions compatible branches and patches.',
      array('@versions' => implode(', ', $versions))),
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
    db_insert('pift_project')
      ->fields(array('pid' => $form_state['values']['nid']))
      ->execute();
  }
  else if (!$form_state['values']['pift_enable'] && $enabled) {
    db_delete('pift_project')
      ->condition('pid', $form_state['values']['nid'])
      ->execute();
  }
}

/**
 * Confirm re-test.
 *
 * @param integer Test ID to re-test.
 */
function pift_pages_retest_confirm_form($form, &$form_state, $test_id) {
  $test = pift_test_get($test_id);

  if ($test) {
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
  global $user;

  $test = $form_state['values']['test'];

  if ($test['status'] > PIFT_STATUS_SENT) {
    // Base changes to be made in followup comment.
    $changes = array(
      'nid' => $test['nid'],
      'uid' => $user->uid,
      'comment' => theme('pift_auto_followup', array('type' => 'retest', 'nid' => $test['nid'], 'cid' => $test['cid'], 'filename' => $test['title'])),
    );

    // If node issue status is not already a valid status then set to default
    // retest status, otherwise leave status alone.
    $node = node_load($test['nid']);
    if (!in_array($node->project_issue['sid'], variable_get('pift_status', array()))) {
      $changes['sid'] = PIFT_FOLLOWUP_RETEST;
    }

    // Add followup to issue.
    project_issue_add_auto_followup($changes);

    // Update test record to reflect change.
    $affected = db_update('pift_test')
      ->fields(array('status', PIFT_STATUS_QUEUE))
      ->condition('test_id', $test['test_id'])
      ->execute();

    if ($affected) {
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
 * @param boolean $closed Issue is closed.
 * @return string HTML output.
 */
function theme_pift_attachments($variables) {
  $files = $variables['files'];
  $closed = $variables['closed'];
  $header = array(t('Attachment'), t('Size'), t('Status'), t('Test result'), t('Operations'));
  $rows = array();
  foreach ($files as $file) {
    $row = array();

    $row[] = l($file['filename'], file_create_url($file['filepath']));
    $row[] = format_size($file['filesize']);

    if ($file['test_id'] !== NULL) {
      list($row[], $class) = pift_attachment_process($file['status'], (bool) $file['message']);
      $message = $file['message'] ? check_plain($file['message']) : '<em>' . t('None') . '</em>';

      // If file is being re-tested then prefix the previous result message.
      if ($file['message'] && $file['status'] == PIFT_STATUS_QUEUE) {
        $message = t('Previous result: ') . $message;
      }

      $row[] = $message;
    }
    else {
      $row[] = '<em>' . t('Ignored') . '</em>';
      $row[] = '<em>' . t('None') . '</em>';
      $class = '';
    }

    // Add valid operations.
    $operations = array();
    if ($file['status'] > PIFT_STATUS_QUEUE || $file['message']) {
      $operations[] = l(t('View details'), PIFT_SERVER . 'pifr/test/' . $file['test_id']);
    }
    if ($file['status'] > PIFT_STATUS_SENT && !$closed) {
      $operations[] = l(t('Re-test'), 'pift/retest/' . $file['test_id']);
    }
    $row[] = ($operations ? implode(' | ', $operations) : '<em>' . t('None') . '</em>');

    $rows[] = array('class' => array($class), 'data' => $row);
  }
  return theme('table', array('header' => $header, 'rows' => $rows));
}

/**
 * Process the specified attachment information and return the valid status
 * message and and row CSS class.
 *
 * @param integer $status Test status, PIFT_STATUS_*.
 * @param boolean $has_message Attachment has a message.
 * @return array Cell value and CSS class.
 */
function pift_attachment_process($status, $has_message) {
  $class = '';
  if ($status == PIFT_STATUS_QUEUE && $has_message) {
    $cell = t('Queued for re-testing');
    $class = 'pift-retest';
  }
  else if ($status == PIFT_STATUS_QUEUE) {
    $cell = t('Queued for testing');
  }
  elseif ($status == PIFT_STATUS_SENT) {
    $cell = t('Test request sent');
    $class = 'pift-retest';
  }
  elseif ($status > PIFT_STATUS_SENT) {
    $cell = t('Idle');
    $class = $status == PIFT_STATUS_PASS ? 'pift-pass' : 'pift-fail';
  }
  return array($cell, $class);
}
