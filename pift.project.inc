<?php
// $Id$
/**
 * @file
 * Provide project related functions.
 *
 * Copyright 2008-2009 by Jimmy Berry ("boombatower", http://drupal.org/user/214218)
 */

/**
 * Check if the project has testing enabled.
 *
 * @param integer $pid Project NID.
 * @return boolean Project has testing enabled.
 */
function pift_project_enabled($pid) {
  $nid = db_result(db_query('SELECT pid
                             FROM {pift_project}
                             WHERE pid = %d', $pid));
  return $nid !== FALSE;
}
