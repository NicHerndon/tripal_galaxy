<?php

function tripal_galaxy_admin_auto_users_page($form, &$form_state){
  // set the breadcrumb
  $breadcrumb = array();
  $breadcrumb[] = l('Home', '<front>');
  $breadcrumb[] = l('Administration', 'admin');
  $breadcrumb[] = l('Tripal', 'admin/tripal');
  $breadcrumb[] = l('Extensions', 'admin/tripal/extension');
  $breadcrumb[] = l('Galaxy', 'admin/tripal/extension/galaxy');
  $breadcrumb[] = l('Users', 'admin/tripal/extension/galaxy/users');
  drupal_set_breadcrumb($breadcrumb);

  // Get some basic stats
  $headers = array('user', 'galaxy user', 'galaxy server');
  // Show up to the first 25 entries on the join of the tripal_galaxy_users,
  //  tripal_galaxy, user tables.

  $rows = array();

  $form['search'] = array(
    '#type' => 'textfield',
    '#title' => t('Filter by Galaxy server or Tripal user'),
    '#description' => t('Press enter to search'),
    '#size' => 15,
    '#ajax' => array(
      'keypress' => TRUE,
      'wrapper' => 'association_table',
      'method' => 'replace',
      'callback' => 'tripal_galaxy_admin_galaxy_users_submit',
    ),
  );

  // Render the table array.
  $form['table'] = array(
    '#prefix' => '<div id="association_table">',
    '#suffix' => '</div>',
    '#type' => 'fieldset',
  );

  // Display all of the entries by default for the admin to browse
  $sql = "
    SELECT DISTINCT U.name, TGU.galaxy_user, TG.servername
    FROM ((tripal_galaxy_user TGU
    INNER JOIN users U ON U.uid = TGU.uid)
    INNER JOIN tripal_galaxy TG ON TG.galaxy_id = TGU.galaxy_id)
    ";

  $entries = db_query($sql);
  $rows = array();
  while($entry = $entries->fetchObject()){
      $rows[] = array($entry->name, $entry->galaxy_user, $entry->servername);
  }

  $num_per_page = variable_get('tripal_galaxy_user_num_per_page', 25);
  $page = pager_default_initialize(sizeof($rows), $num_per_page);

  $chunks = array_chunk($rows, $num_per_page, TRUE);

  $form['table']['table'] = array(
    '#theme' => 'table',
    '#rows' => $chunks[$page],
    '#header' => $headers,
  );
  $form['table']['pager'] = array(
    '#theme' => 'pager',
  );


  // This is invoked when the a query is made (when the user hits enter)
  if(!empty($form_state['values']['search']) && $form_state['values']['search']){
    $sql = "
      SELECT DISTINCT U.name, TGU.galaxy_user, TG.servername
      FROM ((tripal_galaxy_user TGU
      INNER JOIN users U ON U.uid = TGU.uid)
      INNER JOIN tripal_galaxy TG ON TG.galaxy_id = TGU.galaxy_id)
      WHERE U.name LIKE :filter or TG.servername LIKE :filter
      ";

      $entries = db_query($sql, [':filter' => $form_state['values']['search'] . '%']);
      $rows = array();
      while($entry = $entries->fetchObject()){
          $rows[] = array($entry->name, $entry->galaxy_user, $entry->servername);
      }

      $num_per_page = variable_get('tripal_galaxy_user_num_per_page', 25);
      $page = pager_default_initialize(sizeof($rows), $num_per_page);

      $chunks = array_chunk($rows, $num_per_page, TRUE);

      $form['table']['table'] = array(
        '#theme' => 'table',
        '#rows' => $chunks[$page],
        '#header' => $headers,
        '#empty' => t('There are no mappings that match that name or server')

      );
      $form['table']['pager'] = array(
        '#theme' => 'pager',
      );

  }

  return $form;
}


function tripal_galaxy_admin_galaxy_users_submit($form, $form_state){
  return $form['table'];
}
