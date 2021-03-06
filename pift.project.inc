<?php
// $Id$

/**
 * @file
 * Provide project related functions.
 *
 * @author Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Check if the project has testing enabled.
 *
 * @param integer $pid Project NID.
 * @return boolean Project has testing enabled.
 */
function pift_project_enabled($pid) {
  static $statuses = array();

  if (!isset($statuses[$pid])) {
    $nid = db_query('SELECT pid
                               FROM {pift_project}
                               WHERE pid = :pid', array(':pid' => $pid))->fetchField();
    $statuses[$pid] = ($nid !== FALSE);
  }
  return $statuses[$pid];
}
