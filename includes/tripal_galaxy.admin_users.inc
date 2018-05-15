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
  $headers = array('user', 'galaxy server', 'galaxy user');
  // Show up to the first 25 entries on the join of the tripal_galaxy_users,
  //  tripal_galaxy, user tables.

  $rows = array(array('pie', 'pie', 'pie'),);

  $form['title'] = array(
    '#type' => 'textfield',
    '#title' => t('Filter by server or user'),
    '#size' => 15,
    '#ajax' => array(
      'event' => 'keyup', 
      'effect' => 'fade',
      'wrapper' => 'association_table',
      'method' => 'replace',
      'callback' => 'tripal_galaxy_admin_galaxy_users_submit',
    ),
  //'#default_value' => isset($form_state['storage']['title']) ? $form_state['storage']['title'] : '',
  );

  // Render the table array.
  $form['table'] = array(
    '#prefix' => '<div id="association_table">',
    '#suffix' => '</div>',
    '#type' => 'fieldset',

    // '#theme' => 'table',
    // '#header' => $headers,
    // '#rows' => $rows,
    // '#empty' => t('Empty')
  );

  // $form['smol'] = array(
  //   '#theme' => 'table',
  //   '#header' => $headers,
  //   '#rows' => $rows,
  //   '#empty' => t('Empty')
  // );

  if(!empty($form_state['values']['title']) && $form_state['values']['title']){
    $form['table']['smol'] = array(
      '#theme' => 'table',
      '#header' => $headers,
      '#rows' => $rows,
      '#empty' => t('Empty')
    );
    // dpm($form_state);
    // dpm($form);
  }
  return $form;
}


function tripal_galaxy_admin_galaxy_users_submit($form, $form_state){
  // Run the sql query checking against server name or user name
  // dpm('test');
  // $rows = array(array('pickle', 'pickle', 'pickle'),);
  // // wipe and populate the table with those entries
  // $form['table'] = $rows;
  return $form['table'];
}
