<?php

/**
 * Retreives a GalaxyInstance objects using a galaxy_id
 *
 * @param $galaxy_id
 *   The ID of a galaxy server.
 *
 * @return GalaxyInstance
 *   A galaxyInstance object or FALSE on error.
 *
 */
function tripal_galaxy_get_connection($galaxy_id) {
  // Get the galaxy server for this workflow
  $galaxy_server = db_select('tripal_galaxy', 'tg')
    ->fields('tg')
    ->condition('galaxy_id', $galaxy_id)
    ->execute()
    ->fetchObject();

  $library = libraries_load('blend4php');
  if (array_key_exists('error', $library)) {
    drupal_set_message($library['error message'], 'error');
    return FALSE;
  }
  
  $connect = tripal_galaxy_split_url($galaxy_server->url);
  $galaxy = new GalaxyInstance($connect['host'], $connect['port'], $connect['use_https']);
  $galaxy->setAPIKey($galaxy_server->api_key);
  $error = $galaxy->getErrorType();
  if ($error) {
    return FALSE;
  }
  return $galaxy;
}

/**
 * Retreives a GalaxyInstance objects using a galaxy_id and a drupal_user id.
 *
 * @param $galaxy_id
 *   The ID of a galaxy server.
 * @param $uid
 *   The drupal id of the user.
 *
 * @return GalaxyInstance
 *   A galaxyInstance object or FALSE on error.
 *
 */
function tripal_galaxy_get_user_connection($galaxy_id, $uid) {
  // Get the galaxy server for this workflow
  $galaxy_server = db_select('tripal_galaxy', 'tg')
    ->fields('tg')
    ->condition('galaxy_id', $galaxy_id)
    ->execute()
    ->fetchObject();

  $library = libraries_load('blend4php');
  if (array_key_exists('error', $library)) {
    drupal_set_message($library['error message'], 'error');
    return false;
  }

  $connect = tripal_galaxy_split_url($galaxy_server->url);
  $galaxy = new GalaxyInstance($connect['host'], $connect['port'], $connect['use_https']);
  // Get the user's galaxy server api_key
  $galaxy_api = db_select('tripal_galaxy_user', 'tgu')
    ->fields('tgu')
    ->condition('uid', $uid)
    ->execute()
    ->fetchObject();

  $galaxy->setAPIKey($galaxy_api->api_key);
  $error = $galaxy->getErrorType();
  if ($error) {
    return false;
  }
  return $galaxy;
}


/**
 * Splits a URL to a Galaxy server into the host, port and if HTTPS is required.
 *
 * @param $utl
 *   The URL for the remote galaxy instance.
 *
 * @return
 *   An array with three keys: host, port and use_https.
 */
function tripal_galaxy_split_url($url) {

  // TODO: should this go into blend4php?

  // First check a URL with a port
  $matches = [];
  if (preg_match('/^(.*)\:\/\/(.+?)\:(\d+)\/*$/', $url, $matches)) {
    $protocol = $matches[1];
    $host = $matches[2];
    $port = $matches[3];
    $use_https = FALSE;
    if ($protocol == 'https') {
      $use_https = TRUE;
    }
  }
  // Next check a URL without a port
  else {
    if (preg_match('/^(.*)\:\/\/(.+?)\/*$/', $url, $matches)) {
      $protocol = $matches[1];
      $host = $matches[2];
      $use_https = FALSE;
      $port = 80;
      if ($protocol == 'https') {
        $use_https = TRUE;
        $port = 443;
      }
    }
    // for simple url w/port ie. localhost:8080
    else {
      if (preg_match('/(.*)\:(\d+)\/*$/', $url, $matches)) {
        $use_https = FALSE;
        $host = $matches[1];
        $port = $matches[2];
      }
    }
  }
  return [
    'host' => $host,
    'port' => $port,
    'use_https' => $use_https,
  ];
}

/**
 * Checks and updates the status of a Galaxy workflow.
 *
 * @param $sid
 *   The submission ID of the workflow.
 * @param $force .
 *   If a workflow submission is already completed this function will
 *   quickly return and not check the status again.  Setting the $force
 *   argument to TRUE will force the function to check the status.
 */
function tripal_galaxy_check_submission_status($sid, $force = FALSE) {

  if (!$sid) {
    throw new Exception('tripal_galaxy_check_submission_status(): Please provide an $sid argument');

  }
  if (!is_numeric($sid)) {
    throw new Exception('tripal_galaxy_check_submission_status(): The $sid argument is not numeric');
  }

  $query = db_select('tripal_galaxy_workflow_submission', 'tgws');
  $query->fields('tgws', [
    'invocation_id',
    'galaxy_workflow_id',
    'sid',
    'submit_date',
    'status',
  ]);
  $query->join('tripal_galaxy_workflow', 'tgw', 'tgw.galaxy_workflow_id = tgws.galaxy_workflow_id');
  $query->fields('tgw', ['galaxy_id', 'workflow_id', 'nid']);
  $query->condition('tgws.sid', $sid);
  $submission = $query->execute()->fetchObject();

  // If the job hasn't yet been invoked then skip it.
  if (!$submission->invocation_id) {
    return;
  }

  // If the status is already completed or error then we've already handled this
  // workflow fully and we don't need to check the full status again unless
  // the $force argument is set to TRUE.
  if (($submission->status == 'Completed' or $submission->status == 'Error') and !$force) {
    print "Workflow already handled. Status is: $submission->status\n";
    print "Skipping\n";
    return;
  }

  // Get the node for this submission.
  $node = node_load($submission->nid);

  // Connect to the Galaxy instance.
  $galaxy = tripal_galaxy_get_connection($submission->galaxy_id);
  if (!$galaxy) {
    $error = $galaxy->getError();
    drupal_set_message('Could not connect to Galaxy server. ' . $error['message'], 'error');
    return false;
  }
  // Get the invocation specified.
  $gworkflows = new GalaxyWorkflows($galaxy);
  $invocation = $gworkflows->showInvocations([
    'workflow_id' => $submission->workflow_id,
    'invocation_id' => $submission->invocation_id,
  ]);

  if (!$invocation) {     
    throw new Exception($galaxy->getErrorMessage());
  }

  // Iterate through the invocation steps and look for any steps that
  // are in the error state.
  $job_states = [];
  $end_time = NULL;
  $update_time = date_create_from_format('Y-m-d*G:i:s.ue', $invocation['update_time'] . 'UTC');

  // Get the status of each step.
  $invocation_info = [];
  foreach ($invocation['steps'] as $step) {
    $step_index = $step['order_index'];
    $step_state = '';
    $stderr = '';
    $stdout = '';
    $misc_info = '';
    $step_name = $step['workflow_step_label'];
    $step_state = $step['state'];
    $job_state = '';

    // If this step has a job then get some additional information and
    // add it to our step details.
    $job = NULL;
    if ($step['job_id']) {

      // Get the STDERR and STDOUT for the job.
      $gjobs = new GalaxyJobs($galaxy);
      $job = $gjobs->show([
        'job_id' => $step['job_id'],
        'full' => TRUE,
      ]);
      if (!$job) {
        throw new ErrorException($galaxy->getErrorMessage());
      }
      $stderr = $job['stderr'];
      $stdout = $job['stdout'];

      // Get the tool
      $gtools = new GalaxyTools($galaxy);
      $tool = $gtools->show(['tool_id' => $job['tool_id']]);
      if (!$tool) {
        throw new ErrorException($galaxy->getErrorMessage());
      }
      $step_name = $tool['name'];
      $job_state = $job['state'];
      $job_states[$step_index] = $job_state;
    }

    if ($step['state'] == 'error') {
      // Sometimes the error message is in a dataset. So, get the 'misc_info'
      // from that.
      if ($job and $job['outputs']['output']['id']) {
        $gdataset = new GalaxyDatasets($galaxy);
        $dataset = $gdataset->show(['dataset_id' => $job['outputs']['output']['id']]);
        if (!$dataset) {
          throw new ErrorException($galaxy->getErrorMessage());
        }
        $misc_info = $dataset['misc_info'];
      }
    }

    $job_id = $step['job_id'] ? $step['job_id'] : 'NULL';
    $invocation_info[$step_index][$job_id] = [
      'step_name' => $step_name,
      'step_id' => $step['id'],
      'job_id' => $job_id,
      'state' => $job_state,
      'stderr' => $stderr,
      'stdout' => $stdout,
      'misc_info' => $misc_info,
    ];
  } // end foreach ($invocation['steps'] as $step) { ...

  $status = 'Waiting';
  if (in_array('new', array_values($job_states))) {
    $status = 'Waiting';
  }
  if (in_array('scheduled', array_values($job_states))) {
    $status = 'Scheduled';
  }
  if (in_array('running', array_values($job_states))) {
    $status = 'Running';
  }
  if (in_array('error', array_values($job_states))) {
    $status = 'Error';
  }
  $num_jobs = count(array_keys($job_states));
  if (count(preg_grep('/^ok$/', array_values($job_states))) == $num_jobs and $num_jobs > 0) {
    $status = 'Completed';
  }

  // Now deal with the results if this is an error or a completed workflow.
  if ($status == 'Error') {
    print "Job Failed.\n";
    $status = 'Error';
    if (!$force) {
      tripal_galaxy_send_submission_failed_mail($sid, $node->nid);
    }
  }
  if ($status == 'Completed') {
    // Get the history contents if the job step status is 'Completed'.
    $history_contents = [];

    // Find the History for this submission.
    $history_name = tripal_galaxy_get_history_name($submission, $node);
    $ghistories = new GalaxyHistories($galaxy);
    $histories = $ghistories->index();
    $history = '';
    if ($histories) {
      foreach ($histories as $index => $temp) {
        if ($temp['name'] == $history_name) {
          $history = $temp;
        }
      }
    }

    if ($history) {
      $ghistory_contents = new GalaxyHistoryContents($galaxy);
      $history_contents = $ghistory_contents->index(['history_id' => $history['id']]);
    }

    // Get more details about each history content item.
    foreach ($history_contents as $index => $history_content) {
      $history_contents[$index] = $ghistory_contents->show([
        'id' => $history_content['id'],
        'history_id' => $history['id'],
      ]);
      switch ($history_content['type']) {
        case 'file':
          $params = [];
          $params['history_id'] = $history['id'];
          $params['url_only'] = TRUE;
          $params['history_content_id'] = $history_content['id'];
          $link = $ghistory_contents->download_history_content($params);
          $history_contents[$index]['content_link'] = $link;
          break;
        default:
          break;
      }
    }

    $invocation_info['history'] = $history;
    $invocation_info['history_contents'] = $history_contents;

    // Now inform the user that the job is done!
    $end_time = $update_time->getTimestamp();
    if (!$force) {
      tripal_galaxy_send_submission_ended_mail($sid, $node->nid);
    }
  }

  $fields = [
    'status' => $status,
    'errors' => serialize($invocation_info),
  ];
  if ($end_time != NULL) {
    $fields['end_time'] = $end_time;
  }
  db_update('tripal_galaxy_workflow_submission')
    ->fields($fields)
    ->condition('sid', $sid)
    ->execute();
  return TRUE;
}


/**
 * Constructs the history name for a given submission.
 *
 * @param $submission
 *    A submission object that contains the galaxy_workflow_id, sid,
 *    and submit_date properties.
 * @param $node
 *    The sumbission node object.
 *
 * @return
 *   The history name.
 */
function tripal_galaxy_get_history_name($submission, $node) {
  return "TG-" . $node->uid . "-" . $submission->galaxy_workflow_id . "-" . $submission->sid . '-' . date('Y_m_d_H:i:s', $submission->submit_date);
}

/**
 * Invokes all submitted workflows that are in the 'Waiting' state.
 *
 * This function can be called by the tripal Job system hence the $job
 * argument. For Tripal v2 the job_id is passed, for Tripal v3 a job
 * object is passed so we'll handle both cases.
 *
 * @param $sid
 *   The submission ID
 * @param $job
 *   A Tripal v3 Job object or a Tripal v2 job ID.
 */
function tripal_galaxy_invoke_workflow($sid, $job = NULL) {
  // Get the workflows that need execution
  $query = db_select('tripal_galaxy_workflow_submission', 'tgws');
  $query->fields('tgws');
  $query->join('tripal_galaxy_workflow', 'tgw', 'tgw.galaxy_workflow_id = tgws.galaxy_workflow_id');
  $query->fields('tgw', ['galaxy_id', 'workflow_id', 'nid', 'galaxy_workflow_id']);
  $query->condition('tgws.sid', $sid);
  $submission = $query->execute()->fetchObject();
  print_r($submission);
  if (!$submission) {
    throw new Exception('The provided submission ID does not exist');
  }

  // Before we invoke the workflow check if the site is setup to support user
  // submitted workflows.
  if (tripal_galaxy_auto_user_status($submission->galaxy_id)) {
    // First see if the submitting user has a tripal_galaxy_user profile.
    // Because the invocation is handled as a job we cannot rely on the global
    // $user variable, we'll need to query the webform_submissions table.
    $query = db_select('webform_submissions', 'ws');
    $query->fields('ws');
    $query->condition('ws.sid', $sid);
    $webform_submission = $query->execute()->fetchObject();

    $galaxy_user = tripal_get_galaxy_user($webform_submission->uid);
    print_r("Galaxy user $galaxy_user");
    if (empty($galaxy_user)) {
      tripal_create_galaxy_user($webform_submission->uid, $submission->galaxy_id, TRUE);
    }
    print_r("tripal_create_galaxy_user user");
    $galaxy = tripal_galaxy_get_user_connection($submission->galaxy_id, $webform_submission->uid);
    print_r("galaxy user $galaxy");
    if (!$galaxy) {
      $error = $galaxy->getError();
      throw new Exception('Cannot connect to Galaxy server:' . $error['message']);
    }

    // Check to see if a user workflow has been created
    $workflow = tripal_galaxy_workflow_user_status($submission->galaxy_workflow_id, $uid);
    print_r("workflow $workflow");
    // Build the workflow on the galaxy server for the user.
    $galaxy_admin = tripal_galaxy_get_connection($submission->galaxy_id);
    print_r("galaxy_admin $galaxy_admin");
    if (!$galaxy_admin) {
      $error = $galaxy->getError();
      throw new Exception('Cannot connect to Galaxy server:' . $error['message']);
    }
    $gworkflows = new GalaxyWorkflows($galaxy_admin);
    print_r("gworkflows $gworkflows");

    $params = [
      'workflow_id' => $submission->workflow_id
    ];

    $workflow_data = $gworkflows->download($params);
    print_r("workflow_data \n\n");
    print_r($workflow_data);
    $workflow_info = [
      'workflow' => $workflow_data,
    ];

    $user_workflow = $gworkflows->create($workflow_info);
    print_r($user_workflow);
    // Check if the user workflow exists, if not create it.
    $values = [
      'uid' => $galaxy_user,
      'galaxy_workflow_id' => $submission->galaxy_workflow_id,
      'workflow_id' => $user_workflow['id'],
    ];

    try {
      $user_workflow_insert = db_insert('tripal_galaxy_workflow_user')
        ->fields($values)
        ->execute();
      print_r("user_workflow_insert $user_workflow_insert");
    } catch (PDOException $e) {
      return false;
    }
  }
  else {
  // Get a connection to the remote galaxy instance.
    $galaxy = tripal_galaxy_get_connection($submission->galaxy_id);
    if (!$galaxy) {
      $error = $galaxy->getError();
      throw new Exception('Cannot connect to Galaxy server:' . $error['message']);
    }
  }

  // The node for this submission
  $node = node_load($submission->nid);
  print_r("node $node");
  // The array where the workflow input parameters are stored.
  $parameters = [];

  $query = db_select('webform_submitted_data', 'wsd');
  $query->fields('wsd');
  $query->join('webform_component', 'wc', 'wc.cid = wsd.cid');
  $query->fields('wc', ['form_key', 'type', 'extra']);
  $query->condition('wsd.sid', $submission->sid);
  $query->condition('wc.nid', $submission->nid);
  $query->orderBy('wsd.cid');
  $submitted_data = $query->execute()->fetchAll();

  // Get the history that we'll use for this submission.
  $error = [];
  $history_name = tripal_galaxy_get_history_name($submission, $node);
  $history = tripal_galaxy_get_history($galaxy, $history_name, $error);
  if (!$history) {
    $error = $galaxy->getError();
    throw new Exception($error['message']);
  }

  // Get the history contents so we don't upload the same file more than once
  // in the event that this invocation occurs more than once.
  $ghistory_contents = new GalaxyHistoryContents($galaxy);
  $history_contents = $ghistory_contents->index(['history_id' => $history['id']]);
  if ($error['message']) {
    $error = $galaxy->getError();
    throw new Exception($error['message']);
  }

  // Iterate through our submitted data to look for files that need uploading.
  // For those in a dataset collection, create those collections as well.
  $collections = [];
  $inputs = [];
  foreach ($submitted_data as $data) {

    // Handle a single file upload.
    if ($data->type == 'galaxy_sfile') {
      if ($data->no == 'data_collection' or $data->no == 'existing' or preg_match('/^submitted_/', $data->no)) {
        $fid = $data->data;
        if ($fid) {
          $file = tripal_galaxy_upload_file($galaxy, $fid, $history['id'], $history_contents);
          $inputs[$data->form_key] = $file;
        }
      }
      else {
        if ($data->no == 'site_wide') {
          $fid = $data->data;
          if ($fid) {
            $swfile = db_select('tripal_galaxy_site_files', 'tgsf')
              ->fields('tgsf')
              ->condition('fid', $fid)
              ->execute()
              ->fetchObject();
            if (!$swfile) {
              throw new Exception('Cannot find site-wide file with fid: ' . $fid);
            }
            // If this is a remote file....
            if ($swfile->url) {

            }
            // not a remote file.
            else {
              $file = tripal_galaxy_upload_file($galaxy, $fid, $history['id'], $history_contents);
              $inputs[$data->form_key] = $file;
            }
          }
        }
      }
    }
    else {
      if ($data->type == 'galaxy_sflist') {

        if ($data->no == 'existing' or preg_match('/^submitted_/', $data->no)) {
          $fids = $data->data;
          if (!$fids) {
            continue;
          }
          $set = explode('|', $fids);

          // Create the paired dataset.
          $collections[$data->form_key] = [
            'collection_type' => 'list',
            'instance_type' => 'history',
            'name' => $data->form_key,
            'history_id' => $history['id'],
            'element_identifiers' => [],
          ];

          for ($i = 0; $i < count($set); $i++) {
            // Upload each file.
            if ($set[$i]) {
              $dataset1 = tripal_galaxy_upload_file($galaxy, $set[$i], $history['id'], $history_contents);

              // Next add the element.
              $collections[$data->form_key]['element_identifiers'][] = [
                'id' => $dataset1['id'],
                'name' => $dataset1['name'],
                'src' => 'hda',
              ];
            }
          } // end for ($i = 0; $i < count($set1); $i++) {
        }
      }
      else {
        if ($data->type == 'galaxy_pflist') {
          if ($data->no == 'existing' or preg_match('/^submitted_/', $data->no)) {
            $fids = $data->data;
            if (!$fids) {
              continue;
            }
            $set1 = [];
            $set2 = [];
            if ($fids) {
              list($set1, $set2) = explode(',', $fids);
              $set1 = explode('|', $set1);
              $set2 = explode('|', $set2);
            }

            // Create the paired dataset.
            $collections[$data->form_key] = [
              'collection_type' => 'list:paired',
              'instance_type' => 'history',
              'name' => $data->form_key,
              'history_id' => $history['id'],
              'element_identifiers' => [],
            ];

            for ($i = 0; $i < count($set1); $i++) {
              // Upload each file.
              if ($set1[$i] and $set2[$i]) {
                $dataset1 = tripal_galaxy_upload_file($galaxy, $set1[$i], $history['id'], $history_contents);
                $dataset2 = tripal_galaxy_upload_file($galaxy, $set2[$i], $history['id'], $history_contents);

                // Next create the paired dataset collection array.
                $collections[$data->form_key]['element_identifiers'][] = [
                  'name' => $dataset1['name'] . ' and ' . $dataset2['name'],
                  'collection_type' => 'paired',
                  'src' => 'new_collection',
                  'element_identifiers' => [
                    [
                      'id' => $dataset1['id'],
                      'name' => 'forward',
                      'src' => 'hda',
                    ],
                    [
                      'id' => $dataset2['id'],
                      'name' => 'reverse',
                      'src' => 'hda',
                    ],
                  ],
                ];
              }
            } // end for ($i = 0; $i < count($set1); $i++) {
          } // end if ($data->no == 'existing' or preg_match('/^submitted_/', $data->no)) {
        }
      }
    } // end else if($data->type == 'galaxy_pflist') {
  } // foreach ($submitted_data as $data) {

  // Now create a dataset collections
  foreach ($collections as $form_key => $ds_details) {

    // Check for the dataset in the history before trying to readd it.
    $found_collection = FALSE;
    foreach ($history_contents as $hcontent) {
      if ($hcontent['name'] == $form_key and $hcontent['deleted'] == 0) {
        $inputs[$form_key] = $hcontent;
        $found_collection = TRUE;
      }
    }
    if ($found_collection) {
      $report = "Collection, $form_key, already exists.";
      if (is_object($job)) {
        $job->logMessage($report);
      }
      else {
        print $report . "\n";
      }
      continue;
    }

    // Create the collection!
    $report = "Creating Collection: $form_key";
    if (is_object($job)) {
      $job->logMessage($report);
    }
    else {
      print $report . "\n";
    }
    $gds_collection = new GalaxyDatasetCollections($galaxy);
    $ds_collection = $gds_collection->create($ds_details);
    if (!$ds_collection) {
      $error = $galaxy->getError();
      throw new Exception($error['message']);
    }
    $inputs[$form_key] = $ds_collection;
  }

  // Next we need to re-get the history content to get the IDs of the new files.
  $history_contents = $ghistory_contents->index(['history_id' => $history['id']]);
  $input_datasets = [];

  // Iterate through the submitted data values and create an input aray
  // for Galaxy workflow submission.
  foreach ($submitted_data as $data) {
    $value = '';

    // The component 'extra' data is serialized so we need to convert it.
    $data->extra = unserialize($data->extra);

    // Get some step details.
    $step_index = $data->extra['step_index'];
    $input_name = $data->extra['input_full_name'];

    // Skip components that don't have an 'input_name' field as these
    // don't correspond to workflow input components (e.g. fieldsets, and
    // page separators).
    if (!array_key_exists('input_name', $data->extra)) {
      continue;
    }

    // Input elements of type 'fixed_value' should not be provided as
    // input for the Galaxy workflow because the defaults should be used
    if ($data->type == 'fixed_value') {
      $value = $data->data;
      if (array_key_exists('linked', $data->extra) and $data->extra['linked']) {
        list($source_step, $step_output) = explode('|', $data->data);
        $value = [
          'step_output' => $step_output,
          'source_step' => $source_step,
        ];
      }
    }
    else {
      if ($data->type == 'galaxy_sfile') {
        $file = $inputs[$data->form_key];
        foreach ($history_contents as $hcontent) {
          if ($hcontent['name'] == $file['name']) {
            $value = $hcontent['id'];
            $input_datasets[$step_index] = [
              'id' => $value,
              'src' => 'hda',
            ];
          }
        }
      }
      else {
        if ($data->type == 'galaxy_sflist') {
          $dataset = $inputs[$data->form_key];
          $value = $dataset['id'];
          $input_datasets[$step_index] = [
            'id' => $value,
            'src' => 'hdca',
          ];
        }
        else {
          if ($data->type == 'galaxy_pflist') {
            $dataset = $inputs[$data->form_key];
            $value = $dataset['id'];
            $input_datasets[$step_index] = [
              'id' => $value,
              'src' => 'hdca',
            ];
          }
          else {
            $value = $data->data;
          }
        }
      }
    }

    // If we don't have a value then skip it and let the workflow use it's
    // own defaults
    if (!$value) {
      continue;
    }

    if (!array_key_exists($step_index, $parameters)) {
      $parameters[$step_index] = [];
    }
    $current_values = $parameters[$step_index];
    _tripal_galaxy_workflow_invoke_build_values($current_values, $input_name, $value);
    $parameters[$step_index] = $current_values;
  }

  // Before we invoke the workflow check if the site is setup to support user
  // submitted workflows.
  if (tripal_galaxy_auto_user_status($submission->galaxy_id)) {
    tripal_galaxy_workflow_run($workflow->workflow_id, $galaxy, $submission, $parameter, $input_datasets, $history); 
  }
  else {
    tripal_galaxy_workflow_run($submission->workflow_id, $galaxy, $submission, $parameter, $input_datasets, $history); 
  }
}


/**
 * Uploads a file to a given history on Galaxy.
 *
 * @param $galaxy
 *   An instance of a Galaxy server object.
 * @param $fid
 *   The Drupal managed file ID.
 * @param $history_id
 *   The history ID.
 * @param $history_contents
 *   The Galaxy history contents array.
 *
 * @throws Exception
 * @return
 *   An array of the dataset details from Galaxy for the uploaded file.
 */
function tripal_galaxy_upload_file($galaxy, $fid, $history_id, $history_contents) {

  if (!$fid) {
    throw new Exception('Cannot upload a file without an fid');
  }

  $file = file_load($fid);
  $uploaded_file = NULL;


  foreach ($history_contents as $hfile) {
    if (!$hfile['deleted'] and $hfile['state'] == 'ok' and $hfile['name'] == $file->filename) {
      $uploaded_file = $hfile;
    }
  }

  // Only upload the file if it isn't already there.
  if (!$uploaded_file) {
    $file_list = [];
    $file_list[] = [
      'name' => $file->filename,
      'path' => drupal_realpath($file->uri),
    ];
    // Now upload the files.
    $report = "Uploading $file->filename...";
    if (is_object($job)) {
      $job->logMessage($report);
    }
    else {
      print $report . "\n";
    }
    $gtool = new GalaxyTools($galaxy);
    $tool = $gtool->create([
      'tool_id' => 'upload1',
      'history_id' => $history_id,
      'files' => $file_list,
    ]);
    if (!$tool) {
      $error = $galaxy->getError();
      throw new Exception($error['message']);
    }
    return $tool['outputs'][0];
  }
  else {
    $report = "File already exists in history: $file->filename...";
    if (is_object($job)) {
      $job->logMessage($report);
    }
    else {
      print $report . "\n";
    }
    return $uploaded_file;
  }
}

/**
 * Retrieves a history by name from Galaxy.
 *
 * @param $galaxy
 *   A GalaxyInstance object
 * @param $history_name
 *   The name of the history to retrieve. If the history doesn't exist then
 *   it will be created.
 * @param $error
 *   An empty array into which the error type and message will be placed
 *   if an error occurs.
 *
 * @return
 *   A history array for the specified history.  If a failure occured then
 *   FALSE is returned and the $error argument is set.
 */
function tripal_galaxy_get_history(GalaxyInstance $galaxy, $history_name, &$error) {

  // TODO: should this go into blend4php?

  $ghistories = new GalaxyHistories($galaxy);

  // Look through existing histories to find what we're looking for.
  $histories = $ghistories->index();
  if (!$histories) {
    $error = $galaxy->getError();
    throw new Exception($error['message']);
  }
  foreach ($histories as $history) {
    if ($history['name'] == $history_name) {
      return $history;
    }
  }

  // If we're here then the history doesn't exist, so create one.
  $history = $ghistories->create([
    'name' => $history_name,
  ]);
  if (!$history) {
    $error = $galaxy->getError();
    return FALSE;
  }
  return $history;
}

/**
 * Tests if a Galaxy server is accessible.
 *
 * @param $connect
 *   An array of the following:
 *    - galaxy_id: A unique ID for the galaxy server. If this is provided no
 *        other arguments are needed.
 *    - host: The DNS hostname of the galaxy server.
 *    - port: The TCP port for the server.
 *    - use_https:  Set to TRUE of the server uses HTTPS
 *
 * @param
 *   Returns TRUE if accessible. FALSE otherwise.  A Drupal message is
 *   also provided that indicates if the test was successful.
 */
function tripal_galaxy_test_connection($connect) {

  $library = libraries_load('blend4php');
  if (array_key_exists('error', $library)) {
    drupal_set_message($library['error message'], 'error');
    return FALSE;
  }

  if (array_key_exists('galaxy_id', $connect)) {
    $galaxy_id = $connect['galaxy_id'];
    $galaxy = tripal_galaxy_get_connection($galaxy_id);
    $error = $galaxy->getError();
    if ($error['message']) {
      drupal_set_message('Could not connect: ' . $error['message'], 'error');
      return FALSE;
    }
  }
  else {
    // Create a new galaxy instance using the obtained hostname and port
    // Then Authenticate
    $galaxy = new GalaxyInstance($connect['host'], $connect['port'], $connect['use_https']);
    $error = $galaxy->getError();
    if ($error['message']) {
      drupal_set_message('Could not connect: ' . $error['message'], 'error');
      return FALSE;
    }
  }

  // Tell the user whether the connection was successful based on
  // getVersion()
  $version = $galaxy->getVersion();
  if ($version == NULL) {
    $error = $galaxy->getError();
    drupal_set_message('Could not connect: ' . $error['message'], 'error');
    return FALSE;
  }
  else {
    drupal_set_message(t('Successful connection to the Galaxy server (version %version)', ['%version' => $version['version_major']]));
  }
  return TRUE;
}


/**
 * Returns the URI where the Tripal Galaxy module stores files.
 *
 * This function also ensures that the path exists by creating it.
 *
 * @return
 *   A Drupal URI indicating the location where Galaxy files are housed.
 *   Returns FALSE if the location does not exist or cannot be created.
 */
function tripal_galaxy_get_files_dir() {
  global $user;

  $user_uid = md5($user->uid);

  $site_dir = 'public://tripal/files/galaxy/' . $user_uid;
  if (!file_prepare_directory($site_dir, FILE_CREATE_DIRECTORY)) {
    $message = 'Could not access the directory on the server for storing this file.';
    watchdog('tripal', $message, [], WATCHDOG_ERROR);
    return FALSE;
  }

  return $site_dir;
}

/**
* Check the tripal_galaxy_user table for associations of galaxy servers the
* Tripal user is trying to use
*/
function tripal_get_galaxy_user($uid){
  $sql = "
    SELECT * FROM tripal_galaxy_user WHERE tripal_galaxy_user.uid = :uid
  ";
  $entries = db_query($sql, [':uid' => $uid]);
  $array_entries = array();
  while($entry = $entries->fetchObject()){
    $array_entries[] = $entry;
  }
  return $array_entries;
}

// Derived from https://stackoverflow.com/questions/6101956/generating-a-random-password-in-php/31284266#31284266
/**
 * Generate a random string, using a cryptographically secure
 * pseudorandom number generator (random_int)
 *
 * For PHP 7, random_int is a PHP core function
 * For PHP 5.x, depends on https://github.com/paragonie/random_compat
 *
 * @param int $length      How many characters do we want?
 * @param string $keyspace A string of all possible characters
 *                         to select from
 * @return string
 */
function random_str(
    $length,
    $keyspace = '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ'
) {
    $str = '';
    $max = mb_strlen($keyspace, '8bit') - 1;
    if ($max < 1) {
        throw new Exception('$keyspace must be at least two characters long');
    }
    for ($i = 0; $i < $length; ++$i) {
        $str .= $keyspace[random_int(0, $max)];
    }
    return $str;
}

/**
* Create a galaxy user based off of the following criteria
* $uid:     The drupal user uid to add the association to
* $managed: If TRUE then generate new username, password, and dummy email
*           address
* $gsid:    The galaxy server to add this user on
* $auth:    This is an associative array populated with user provided
*           'email', and 'password' if managed is FALSE
*           If the auth array is not accepted this will return FALSE instead
*           of a PHP object
*
* @return PHP objcet containing the row entry on tripal_galaxy_user table
*
*/
function tripal_create_galaxy_user($uid, $gsid, $managed = TRUE, $auth = NULL){

  // Connect to the galaxy server's with $gsid
  $galaxy = tripal_galaxy_get_connection($gsid);

  // Tripal created
  if($managed){
    $server = db_select('tripal_galaxy', 'tg')
      ->fields('tg')
      ->condition('galaxy_id', $gsid)
      ->execute()
      ->fetchObject();

    $params = array();
    date_default_timezone_set("UTC");
    // get the drupal username and prepend 'galaxy_' as the galaxy username
    $user = db_select('users', 'u')
      ->fields('u')
      ->condition('uid', $uid)
      ->execute()
      ->fetchObject();
    $params['username'] = 'galaxy_' . $user->name . '_' . time();
    // generate a password
    $params['password'] = hash("sha512", random_str(64));
    // generate a dummy email addr
    $params['email'] = random_str(16) . '@localhost.localdomain';

    // API call to make a new user on the given galaxy server
    $users = new GalaxyUsers($galaxy);

    // SUCCESS in creating a new user on specified galaxy server
    $new_user = $users->create($params);

    // ERROR
    if(!$new_user)
      throw new Exception($galaxy->getErrorMessage());

    // By galaxy default the new_user will not have an API key so generate one
    $api_key = $users->apiKey(array('user_id' => $new_user['id']));

    // ERROR
    if(!$api_key)
      throw new Exception($galaxy->getErrorMessage());

    // We have all the necessary information now to associate this user to
    // Tripal and add this to the tripal_galaxy_user table
    // CATS - All your base are belong to us
    $values = [
      'uid' => $uid,
      'galaxy_user' => $new_user['username'],
      'galaxy_id' => $gsid,
      'mode' => 1,
      'api_key' => $api_key,
    ];

    try {
      $tgu = db_insert('tripal_galaxy_user')
        ->fields($values)
        ->execute();
        // drupal_set_message('Successfully added new user to Galaxy Server: ' . $server->servername);
        return $tgu;
    }
    catch (PDOException $e) {
      // drupal_set_message(t('Error: %message', array('%message' => $e->getMessage())), 'error');
      return FALSE;
    }

  }

  // $managed == FALSE
  // Mode 3, the user was already existing on the server before Tripal
  // connected
  else{
    // else plug in the username and password and attempt to authenticate on
    // given galaxy server
    if($auth){
      // $galaxy_instance = new GalaxyInstance($galaxy);

      $auth = $galaxy->authenticate($auth['email'], $auth['password']);

      // ERROR
      if(!$auth)
        throw new Exception($galaxy->getErrorMessage());

      // this call will either return the current key or generate an api key
      // that will henceforth belong to this user on this galaxy server
      $api_key = $galaxy->getAPIKey();

      // Associate this existing Galaxy user to Tripal and add this to the
      // tripal_galaxy_user table

      $values = [
        'uid' => $uid,
        'galaxy_user' => $new_user['username'],
        'galaxy_id' => $gsid,
        'mode' => 3,
        'api_key' => $api_key,
      ];

      try {
        $tgu = db_insert('tripal_galaxy_user')
          ->fields($values)
          ->execute();
          // drupal_set_message('Successfully added new user to Galaxy Server: ' . $server->servername);
          return $tgu;
      }
      catch (PDOException $e) {
        // drupal_set_message(t('Error: %message', array('%message' => $e->getMessage())), 'error');
        return FALSE;
      }
      // dpm($api_key);
    }
    // if auth array is null return FALSE
    return FALSE;
  }
}


/**
 * Delete a galaxy user from remote galaxt site.
 * $uid:     The drupal user uid to add the association to
 * $gsid:    The galaxy server to add this user on
 *
 * @return Booleans True if successful False if Error
 *
 */
function tripal_delete_galaxy_user($uid, $gsid) {

  // Connect to the galaxy server's with $gsid
  $galaxy = tripal_galaxy_get_connection($gsid);
  $histories = db_select('tripal_galaxy_workflow_user', 'tg')
    ->fields('tg')
    ->condition('uid', $uid)
    ->execute();

  while ($histories->fetchAll()) {
    tripal_galaxy_delete_remote_history($gsid, $histories->workflow_id);
  }

  // API call to make a new user on the given galaxy server.
  $users = new GalaxyUsers($gsid);

  //Get the galaxy username so we can get the galaxy user ID.
  $duser = db_select('tripal_galaxy_user', 'tg')
    ->fields('tg')
    ->condition('uid', $uid)
    ->execute()
    ->fetchObject();

  $userid = $users->getUserID(['user_id' => $duser->galaxy_user]);

  //Now delete the user from the galaxy server.
  $params = ['user_id' => $userid];
  $deleted_user = $users->delete($params);
  
}

/**
 * Walks through the tripal_galaxy_workflow_submission table and deletes any 
 * workflows older than specifed in the tripal_galaxy_history_age system 
 * variable.
 *
 *
 */
function tripal_galaxy_delete_expired_histories(){
  $max_history_age = time() - variable_get('tripal_galaxy_history_age');
  $old_workflows = db_select('tripal_galaxy_workflow_submission', 'tgws')
    ->fields('tgws')
    ->condition('start_time', $max_history_age, '<')
    ->execute();
  while ($old_workflow = $old_workflows->fetchObject()) {

    // Skip already delated workflow invocations.
    if($old_workflow->status == 'Deleted') {
      continue;
    }
    
    $tp_workflow = db_select('tripal_galaxy_workflow', 'tgw')
      ->fields('tgw')
      ->condition('galaxy_workflow_id', $old_workflow->galaxy_workflow_id, '=')
      ->execute()
      ->fetchObject();

    $node = node_load($tp_workflow->nid);
    $history_name = tripal_galaxy_get_history_name($old_workflow, $node);
    $success = tripal_galaxy_delete_remote_history($tp_workflow->galaxy_id, $history_name);
    
    if ($success) {
      drupal_set_message('Successfully deleted workflow invocation: ' . $old_workflow->invocation_id);
      //disable the workflow on the site.
      db_update('tripal_galaxy_workflow_submission')
        ->fields(array(
          'status' => 'Deleted'
        ))
        ->condition('invocation_id', $old_workflow->invocation_id)
        ->execute();
    }
    else {
      drupal_set_message('Failed to deleted workflow invocation: ' . $old_workflow->invocation_id, 'error');
    }
  }
}

/**
 * Deletes a single remote history from the remote galaxy server.
 *
 * @param $galaxy_id: A unique ID for the galaxy server. If this is provided no
 *        other arguments are needed.
 * 
 * @param $history_name
 *   The name of the history to retrieve. If the history doesn't exist then
 *   it will be created.
 * 
 */
function tripal_galaxy_delete_remote_history($galaxy_id, $history_name) {
  $error = [];
  try {
    $galaxy = tripal_galaxy_get_connection($galaxy_id);
    $history = tripal_galaxy_get_history($galaxy, $history_name, $error);
    if (!$history) {
      $error = $galaxy->getError();
      throw new Exception("Cannot find history, $history_name :" . $error['message']);
    }

    $ghistories = new GalaxyHistories($galaxy);
    $deleted = $ghistories->deleteHistory(array('history_id' => $history['id']));
    if (!$deleted) {
      $error = $galaxy->getError();
      throw new Exception("Cannot delete the history, $history_name :" . $error['message']);
    }
    return TRUE;
  }
  catch (Exception $e) {
    drupal_set_message(t('Could not delete the remote history.  Please contact the web site administrator to report this issue.'), 'error');
    watchdog_exception('tripal_galaxy', $e);
    return FALSE;
  }
}

/**
 * Check the auto_user status of the site
 * @param $galaxy_id: A unique ID for the galaxy server. If this is provided no
 *        other arguments are needed.
 * 
 * @return Boolean as stored in the 
 * 
 */
function tripal_galaxy_auto_user_status($galaxy_id) {
  $galaxy = db_select('tripal_galaxy', 'tg')
    ->fields('tg')
    ->condition('galaxy_id', array($galaxy_id))
    ->execute()
    ->fetchObject();
  return $galaxy->auto_user;
}

/**
 * Check if this user has this workflow.
 * 
 * @param $galaxy_workflow_id: A unique ID for local workflow submission.
 * TODO: How to handle expired workflows.
 */
function tripal_galaxy_workflow_user_status($galaxy_workflow_id, $uid)
{
  $galaxy_workflow = db_select('tripal_galaxy_workflow_user', 'tg')
    ->fields('tg')
    ->condition('galaxy_workflow_id', array($galaxy_workflow_id))
    ->condition('uid', array($uid))
    ->execute()
    ->fetchObject();

  return $galaxy_workflow;
}

/**
 * Run a workflow on rmeote galaxy
 * 
 * @param $galaxy: A GalaxyInstance object
 * @param $submission: A submission object that contains the galaxy_workflow_id, sid,
 *    and submit_date properties.
 * @param $parameter: An array of the workflow values.
 * @param $input_datasets: An array of the inputs for the dataset.
 * @param $history: An array of history returned from the galaxy server.
 * 
 * 
 */
function tripal_galaxy_workflow_run($workflow_id, $galaxy, $submission, $parameter, $input_datasets, $history) {
  // Invoke the workflow and check for errors
  $gworkflows = new GalaxyWorkflows($galaxy);
  $params = [
    'workflow_id' => $workflow_id,
    'parameters' => $parameters,
    'inputs' => $input_datasets,
    'history_id' => $history['id'],
  ];
  $invocation = $gworkflows->invoke($params);

  if (!$invocation) {
    $error = $galaxy->getError();
    drupal_set_message($error['message'], 'error');
    db_update('tripal_galaxy_workflow_submission')
      ->fields([
        'status' => 'Error',
        'errors' => $error['message'],
      ])
      ->condition('sid', $submission->sid)
      ->execute();
    tripal_galaxy_send_submission_failed_mail($submission->sid, $node->uid);
  } else {
    db_update('tripal_galaxy_workflow_submission')
      ->fields([
        'status' => 'Submitted',
        'start_time' => REQUEST_TIME,
        'end_time' => null,
        'invocation_id' => $invocation['id'],
      ])
      ->condition('sid', $submission->sid)
      ->execute();
    tripal_galaxy_send_submission_start_mail($submission->sid, $node->uid);
  }
}