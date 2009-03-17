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
 * @param integer $project_nid Project NID.
 * @return boolean Project has testing enabled.
 */
function pift_project_enabled($project_nid) {
  $nid = db_result(db_query('SELECT project_nid
                             FROM {pift_project}
                             WHERE project_nid = %d', $project_nid));
  return $nid !== FALSE;
}
