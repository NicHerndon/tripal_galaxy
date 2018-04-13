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

  tripal_add_d3js();
  drupal_add_js(drupal_get_path ('module', 'tripal_galaxy') . '/theme/js/tripal_galaxy.dashboard.js');
  drupal_add_css(drupal_get_path ('module', 'tripal_galaxy') . '/theme/css/tripal_galaxy.dashboard.css');

  // Get some basic stats
  $headers = array();
  $rows = array();

  $form['title'] = array(
  '#type' => 'textfield',
  '#title' => t('Filter by server or user'),
  '#size' => 15,
  //'#default_value' => isset($form_state['storage']['title']) ? $form_state['storage']['title'] : '',
  );

  // Submit button.
  $form['submit'] = array(
    '#type' => 'submit',
    '#value' => t("Find"),
  );

  // Render the table array.
 $form['table'] = array(
   '#theme' => 'table',
   '#header' => 'test',
   '#rows' => $rows,
   '#empty' => t('Empty')
 );
  return $form;
}

function tripal_galaxy_admin_galaxy_users_form(){

}

function tripal_galaxy_admin_galaxy_users_submit(){

}
