<?php
// $Id$

/**
 * @file
 * API functions of Revisioning module
 *
 * Reusable functions that do the dirty work.
 */

define('REVISION_ARCHIVED', 0);
define('REVISION_CURRENT', 1);
define('REVISION_PENDING', 2);

/**
 * Some naming conventions
 *
 * Pending:
 *   - revision with (vid > current_vid) of ANY node
 *     OR single revision of UNPUBLISHED node
 *
 * Current, published:
 *   - revision with (vid == current_vid) of PUBLISHED node
 *
 * Archived:
 *   - all other revisions, i.e.
 *     revision with (vid < current_vid) of ANY node
 *     OR revision with (vid == current_vid) of UNPUBLISHED node
 *
 * Note: these will change when Revisioing is going to store revision states
 * independently from vid number (e.g. in different table).
 */

/**
 * Return a single or all possible revision state names.
 *
 * @param $state
 *  optional state id, as defined in revisioning_api.inc
 * @return
 *  if $state is provided, state name. Otherwise, state names array keyed by state id.
 */
function _revisioning_revision_states($state = NULL) {
  $states = array(
    REVISION_PENDING  => t('Pending'),
    REVISION_CURRENT  => t('Current, published'),
    REVISION_ARCHIVED => t('Archived'),
  );
  return empty($state) ? $states : $states[$state];
}

/**
 * Implementation of hook_revisionapi().
 *
 * Act on various revision events.
 *
 * @param $op
 *  Operation
 * @param $node
 *  Node of current operation (loaded with vid of the operation).
 *
 * "Pre" operations can be useful to get values before they are lost or changed,
 * for example, to save a backup of revision before it's deleted.
 * Also, for "pre" operations vetoing mechanics could be implemented, so it
 * would be possible to veto an operation via hook_revisionapi(). For example,
 * when the hook is returning FALSE, operation will be vetoed.
 *
 * @TODO: Add more operations if needed.
 */
function revisioning_revisionapi($op, &$node) {
  switch ($op) {
    case 'pre revert':
      // Invoke corresponding Rules event
      if (module_exists('rules')) {
        rules_invoke_event('revisioning_pre_revert', $node);
      }
      break;

    case 'post revert':
      // Invoke the revisioning trigger passing 'revert' as the operation
      if (module_exists('trigger')) {
        module_invoke_all('revisioning', 'revert');
      }
      // Invoke corresponding Rules event
      if (module_exists('rules')) {
        rules_invoke_event('revisioning_post_revert', $node);
      }
      break;

    case 'pre publish':
      // Invoke corresponding Rules event
      if (module_exists('rules')) {
        rules_invoke_event('revisioning_pre_publish', $node);
      }
      break;

    case 'post publish':
      // Invoke the revisioning trigger passing 'publish' as the operation
      if (module_exists('trigger')) {
        module_invoke_all('revisioning', 'publish');
      }
      // Invoke corresponding Rules event
      if (module_exists('rules')) {
        rules_invoke_event('revisioning_post_publish', $node);
      }
      break;

  //case 'pre unpublish':
      // Not implemented: do we really need it ?

      case 'post unpublish':
      // Invoke the revisioning trigger passing 'unpublish' as the operation
      if (module_exists('trigger')) {
        module_invoke_all('revisioning', 'unpublish');
      }
      // Invoke corresponding Rules event
      if (module_exists('rules')) {
        rules_invoke_event('revisioning_post_unpublish', $node);
      }
      break;

    case 'pre delete':
      // Invoke corresponding Rules event
      if (module_exists('rules')) {
        rules_invoke_event('revisioning_pre_delete', $node);
      }
      break;

    case 'post delete':
      break;
  }
}

/**
 * Revert node to selected revision without changing its publication status.
 *
 * @param $node
 *  Target $node object (loaded with target revision) or nid of target node
 * @param $vid
 *  Optional vid of revision to revert to, if provided $node must not be an object.
 */
function _revisioning_revertpublish_revision(&$node, $vid = NULL) {
  $node_revision = is_object($node) ? $node : node_load($node, $vid);
  module_invoke_all('revisionapi', 'pre revert', $node_revision);
  _revisioning_revert_revision($node_revision);
  module_invoke_all('revisionapi', 'post revert', $node_revision);
}

/**
 * Revert node to selected revision without publishing it.
 *
 * This is same as node_revision_revert_confirm_submit() in node_pages.inc,
 * except it doesn't put any messages on screen.
 *
 * @param $node
 *  Target $node object (loaded with target revision) or nid of target node
 * @param $vid
 *  optional vid of revision to revert to, if provided $node is not an object.
 */
function _revisioning_revert_revision(&$node, $vid = NULL) {
  $node_revision = is_object($node) ? $node : node_load($node, $vid);
  $node_revision->revision = 1;
  $node_revision->log = t('Copy of the revision from %date.', array('%date' => format_date($node_revision->revision_timestamp)));
  if (module_exists('taxonomy')) {
    $node_revision->taxonomy = array_keys($node_revision->taxonomy);
  }
  node_save($node_revision);
  watchdog('content', '@type: reverted %title revision %revision.', array('@type' => $node_revision->type, '%title' => $node_revision->title, '%revision' => $node_revision->vid));
}

/**
 * Publish node, without calling node_save()
 *
 * @param $node
 *  Target $node object or nid of target node
 */
function _revisioning_publish_node($node) {
  $nid = is_object($node) ? $node->nid : $node;
  db_query("UPDATE {node} SET status=1 WHERE nid=%d", $nid);
  cache_clear_all();
}

/**
 * Unpublish node, without calling node_save().
 *
 * @param $node
 *  Target $node object or nid of target node
 */
function _revisioning_unpublish_node($node) {
  $nid = is_object($node) ? $node->nid : $node;
  db_query("UPDATE {node} SET status=0 WHERE nid=%d", $nid);
  cache_clear_all();
}

/**
 * Delete selected revision of node, provided it's not current.
 *
 * This is same as node_revision_delete_confirm_submit() in node_pages.inc,
 * except it doesn't put any messages on the screen. This way it becomes
 * reusable (eg. in actions).
 * Since we are calling nodeapi as in node_revision_delete_confirm_submit(), we
 * invoke our "post delete" revisionapi hook in nodeapi. This way revisionapi
 * hooks work the same way both with "delete revision" submit handler and when
 * this function is called, and we don't invoke revisionapi "post delete" hook
 * twice.
 *
 * @param $node
 *  Target $node object (loaded with target revision) or nid of target node
 * @param $vid
 *  optional vid of revision to delete, if provided $node is not object.
 *
 * @TODO: Insert check to prevent deletion of current revision of node.
 */
function _revisioning_delete_revision(&$node, $vid = NULL) {
  $node_revision = is_object($node) ? $node : node_load($node, $vid);
  module_invoke_all('revisionapi', 'pre delete', $node_revision);
  db_query("DELETE FROM {node_revisions} WHERE nid = %d AND vid = %d", $node_revision->nid, $node_revision->vid);
  node_invoke_nodeapi($node_revision, 'delete revision');
  watchdog('content', '@type: deleted %title revision %revision.', array('@type' => $node_revision->type, '%title' => $node_revision->title, '%revision' => $node_revision->vid));
}

/**
 * Unpublish revision (i.e. the node).
 *
 * Note that no check is made as to whether the initiating user has permission
 * to unpublish this node.
 *
 * @param $node
 *  Target $node object or nid of target node
 *
 * @TODO: Shouldn't we invoke hook_nodeapi('update'..) too, since we are updating node?
 */
function _revisioning_unpublish_revision(&$node) {
  $node_revision = is_object($node) ? $node : node_load($node);
  module_invoke_all('revisionapi', 'pre unpublish', $node_revision);
  _revisioning_unpublish_node($node_revision->nid);
  watchdog('content', 'Unpublished @type %title', array('@type' => $node_revision->type, '%title' => $node_revision->title), WATCHDOG_NOTICE, l(t('view'), "node/$node_revision->nid"));
  module_invoke_all('revisionapi', 'post unpublish', $node_revision);
}

/**
 * Make the supplied revision of the node current and publish it.
 * It is the caller's responsibility to provide proper revision.
 * Note that no check is made as to whether the initiating user has permission
 * to publish this revision.
 *
 * @param $node
 *  Target $node object (loaded with target revision) or nid of target node
 * @param $vid
 *  optional vid of revision to make current, if provided $node is not object.
 *
 * @TODO: Shouldn't we invoke hook_nodeapi('update'..) too, since we are updating node ?
 */
function _revisioning_publish_revision(&$node, $vid = NULL) {
  $node_revision = is_object($node) ? $node : node_load($node, $vid);
  module_invoke_all('revisionapi', 'pre publish', $node_revision);
  // Update node table, making sure the "published" (ie. status) flag is set
  db_query("UPDATE {node} SET vid=%d, title='%s', status=1 WHERE nid=%d", $node_revision->vid, $node_revision->title, $node_revision->nid);
  cache_clear_all();
  watchdog('content', 'Published rev #%revision of @type %title', array('@type' => $node_revision->type, '%title' => $node_revision->title, '%revision' => $node_revision->vid), WATCHDOG_NOTICE, l(t('view'), "node/$node_revision->nid/revisions/$node_revision->vid/view"));
  module_invoke_all('revisionapi', 'post publish', $node_revision);
}

/**
 * Find the most recent pending revision, make it current, unless it already is
 * and publish node.
 * Note that no check is made as to whether the initiating user has permission
 * to publish this node.
 *
 * @param $node
 *   The node object whose latest pending revision is to be published
 * @return
 *   TRUE if operation was successful, FALSE if there is no pending revision to
 *   publish
 */
function _revisioning_publish_latest_revision(&$node) {
  // Get latest pending revision or take the current provided it's UNpublished
  $latest_pending = array_shift(_get_pending_revisions($node->nid));
  if (!$latest_pending) {
    if (!$node->status && $node->vid == get_current_revision_id($node->nid)) {
      $latest_pending = $node;
    }
  }
  if ($latest_pending) {
    _revisioning_publish_revision($node->nid, $latest_pending->vid);
    return TRUE;
  }
  return FALSE;
}

/**
 * Return a count of the number of revisions newer than the supplied vid.
 *
 * @param $vid
 *  The reference vid.
 * @param $nid
 *  The id of the node.
 * @return
 *  integer
 */
function _get_number_of_revisions_newer_than($vid, $nid) {
  return db_result(db_query("SELECT COUNT(*) FROM {node} n INNER JOIN {node_revisions} r ON n.nid=r.nid WHERE (r.vid>%d AND n.nid=%d)", $vid, $nid));
}

/**
 * Return a count of the number of revisions newer than the current revision.
 *
 * @param $nid
 *  The id of the node.
 * @return
 *  integer
 */
function _get_number_of_pending_revisions($nid) {
  return db_result(db_query("SELECT COUNT(*) FROM {node} n INNER JOIN {node_revisions} r ON n.nid=r.nid WHERE (r.vid>n.vid AND n.nid=%d)", $nid));
}

/**
 * Retrieve a list of revisions with a vid greater than the current.
 *
 * @param $nid
 *  The node id to retrieve.
 * @return
 *  An array of revisions (latest first), each containing vid, title and
 *  content type.
 */
function _get_pending_revisions($nid) {
  $sql = "SELECT r.vid, r.title, n.type FROM {node} n INNER JOIN {node_revisions} r ON n.nid=r.nid WHERE (r.vid>n.vid AND n.nid=%d) ORDER BY r.vid DESC";
  $result = db_query($sql, $nid);
  $revisions = array();
  while ($revision = db_fetch_object($result)) {
    $revisions[$revision->vid] = $revision;
  }
  return $revisions;
}

/**
 * Return whether the node has a pending revision, that is a revision newer
 * than the current OR when there's only one revision, whether that revision is
 * unpublished.
 *
 * @param $node
 * @return
 *  TRUE, if the node has a pending revision.
 */
function _is_pending($node) {
  return ($node->vid > get_current_revision_id($node->nid)) ||
    (!$node->status && get_number_of_revisions($node->nid) == 1) ;
}

/**
 * Retrieve a list of all revisions (archive, current, pending) belonging to
 * the supplied node.
 *
 * @param $nid
 *  The node id to retrieve.
 * @param $include_taxonomy_terms
 *  Whether to also retrieve the taxonomy terms for each revision
 * @return
 *  An array of revision objects, each with published flag, log message, vid,
 *  title, timestamp and name of user that created the revision
 */
function _get_all_revisions_for_node($nid, $include_taxonomy_terms = FALSE) {
  $sql_select = 'SELECT n.type, n.status, r.vid, r.title, r.log, r.uid, r.timestamp, u.name';
  $sql_from   = ' FROM {node_revisions} r LEFT JOIN {node} n ON n.vid=r.vid INNER JOIN {users} u ON u.uid=r.uid';
  $sql_where  = ' WHERE r.nid=%d ORDER BY r.vid DESC';
  if ($include_taxonomy_terms) {
    $sql_select .= ', td.name AS term';
    $sql_from .= ' LEFT JOIN {term_node} tn ON r.vid=tn.vid LEFT JOIN {term_data} td ON tn.tid=td.tid';
    $sql_where .= ', term ASC';
  }
  $sql = $sql_select . $sql_from . $sql_where;
  $result = db_query($sql, $nid);
  $revisions = array();
  while ($revision = db_fetch_object($result)) {
    if (empty($revisions[$revision->vid])) {
      $revisions[$revision->vid] = $revision;
    }
    elseif ($include_taxonomy_terms) {
      // If a revision has more than one taxonomy term, these will be returned
      // by the query as seperate objects differing only in their term fields.
      $existing_revision = $revisions[$revision->vid];
      $existing_revision->term .= '/'. $revision->term;
    }
  }
  return $revisions;
}

/**
 * Return revision type of the supplied node.
 *
 * @param &$node
 *  Node object to check
 * @return
 *  Revision type
 */
function _revisioning_revision_is(&$node) {
  if (_is_pending($node)) {
    return REVISION_PENDING;
  }
  $current_vid = get_current_revision_id($node->nid);
  return ($current_vid == $node->vid) && $node->status ? REVISION_CURRENT : REVISION_ARCHIVED;
}

/**
 * Return a string with details about the node that is about to be displayed.
 *
 * Called from revisioning_nodeapi().
 *
 * @param $node
 *  The node that is about to be viewed
 * @return
 *  A translatable message containing details about the node
 */
function _get_node_info_msg($node) {
  // Get username for the revision, not the creator of the node
  $revision_author = user_load($node->revision_uid);
  $placeholder_data = array(
    '@content_type' => $node->type,
    '%title' => $node->title,
    '!author' => theme('username', $revision_author),
    '@date' => format_date($node->revision_timestamp, 'small'),
  );
  $revision_type = _revisioning_revision_is($node);
  switch ($revision_type) {
    case REVISION_PENDING:
      return t('Displaying <em>pending</em> revision of @content_type %title, last modified by !author on @date', $placeholder_data);

    case REVISION_CURRENT:
      return t('Displaying <em>current, published</em> revision of @content_type %title, last modified by !author on @date', $placeholder_data);

    case REVISION_ARCHIVED:
      return t('Displaying <em>archived</em> revision of @content_type %title, last modified by !author on @date', $placeholder_data);
  }
}