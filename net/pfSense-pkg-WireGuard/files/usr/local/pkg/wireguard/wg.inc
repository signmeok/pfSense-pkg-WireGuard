<?php
/*
 * wg.inc
 *
 * part of pfSense (https://www.pfsense.org)
 * Copyright (c) 2021 Rubicon Communications, LLC (Netgate)
 * Copyright (c) 2021 R. Christian McDonald (https://github.com/theonemcdonald)
 * Copyright (c) 2020 Ascrod
 * All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// pfSense includes
require_once('config.inc');
require_once('gwlb.inc');
require_once('util.inc');

// WireGuard includes
require_once('wireguard/wg_api.inc');
require_once('wireguard/wg_install.inc');
require_once('wireguard/wg_service.inc');
require_once('wireguard/wg_validate.inc');

global $wgg;

/*
 * This toggles a given tunnel based on index
 */
function wg_toggle_tunnel($tunnel_name) {
	global $wgg;

	// Make sure we have the latest info
	wg_globals();

	// Assume no changes will be made...
	$changes = false;

	$input_errors = array();

	if ([$tun_idx, $tunnel, $is_new] = wg_tunnel_get_config_by_name($tunnel_name)) {

		$enabled = ($tunnel['enabled'] == 'yes');

		if ($enabled && is_wg_tunnel_assigned($tunnel['name'])) {

			$wg_pfsense_if = wg_get_pfsense_interface_info($tunnel['name']);

			$input_errors[] = "Cannot disable {$tunnel['name']} while it is assigned to {$wg_pfsense_if['descr']} ({$wg_pfsense_if['name']})";

		}

		if (empty($input_errors)) {

			// Now we toggle the tunnel
			$wgg['tunnels'][$tun_idx]['enabled'] = $enabled ? 'no' : 'yes';

			$action_txt = $enabled ? 'disabled' : 'enabled';
			
			// Sync with configuration backend
			write_config("[{$wgg['pkg_name']}] Tunnel {$tunnel['name']} {$action_txt}.");

			// Resync the package
			wg_resync();

			// We've got meaningful changes...
			$changes = true;

			// What tunnel would we need to sync to apply these changes?
			$tuns_to_sync[] = $tunnel['name'];

		}

	}

	return array('input_errors' => $input_errors, 'changes' => $changes, 'tuns_to_sync' => $tuns_to_sync);

}

/*
 * This toggles a given peer based on config array index
 */
function wg_toggle_peer($peer_idx) {
	global $wgg;

	// Make sure we have the latest info
	wg_globals();

	// Assume no changes will be made...
	$changes = false;

	$input_errors = array();

	if ([$peer_idx, $peer, $is_new] = wg_peer_get_config($peer_idx)) {

		$enabled = ($peer['enabled'] == 'yes');

		// Boilerplate...
		if (empty($input_errors)) {

			// Toggle the peer
			$wgg['peers'][$peer_idx]['enabled'] = $enabled ? 'no' : 'yes';

			$action_text = $enabled ? 'disabled' : 'enabled';

			// Sync with configuration backend
			write_config("[{$wgg['pkg_name']}] Peer {$peer_idx} {$action_text}.");

			// Resync the package
			wg_resync();

			// This checks if the peer tunnel is a valid tunnel
			if (wg_is_valid_tunnel($peer['tun'])) {

				// We've got meaningful changes...
				$changes = true;

				// What tunnel would we need to sync to apply these changes?
				$tuns_to_sync[] = $peer['tun'];

			}

		}

	}

	return array('input_errors' => $input_errors, 'changes' => $changes, 'tuns_to_sync' => $tuns_to_sync);

}

/*
 * This deletes a given peer based on config array index
 */
function wg_delete_peer($peer_idx) {
	global $wgg;

	// Make sure we have the latest info
	wg_globals();

	$changes = false;

	$input_errors = array();

	if ([$peer_idx, $peer, $is_new] = wg_peer_get_config($peer_idx)) {

		// Boilerplate...
		if (empty($input_errors)) {

			// Delete the peer
			unset($wgg['peers'][$peer_idx]);

			// Sync with configuration backend
			write_config("[{$wgg['pkg_name']}] Peer {$peer_idx} deleted.");

			// Resync the package
			wg_resync();

			// This checks if the peer's tunnel was a valid tunnel
			if (wg_is_valid_tunnel($peer['tun'])) {

				// We've got meaningful changes...
				$changes = true;

				// What tunnel would we need to sync to apply these changes?
				$tuns_to_sync[] = $peer['tun'];

			}

		}
		
	}

	return array('input_errors' => $input_errors, 'changes' => $changes, 'tuns_to_sync' => $tuns_to_sync);

}

/*
 * This deletes a given tunnel based on name
 */
function wg_delete_tunnel($tunnel_name) {
	global $wgg;

	// Make sure we have the latest info
	wg_globals();

	$changes = false;

	$input_errors = array();

	if ([$tun_idx, $tunnel, $is_new] = wg_tunnel_get_config_by_name($tunnel_name)) {

		// We can't delete assigned tunnels
		if (is_wg_tunnel_assigned($tunnel['name'])) {

			$wg_pfsense_if = wg_get_pfsense_interface_info($tunnel['name']);

			$input_errors[] = "Cannot delete {$tunnel['name']} while it is assigned to {$wg_pfsense_if['descr']} ({$wg_pfsense_if['name']})";

		}

		if (empty($input_errors)) {

			// Delete the tunnel
			unset($wgg['tunnels'][$tun_idx]);

			// Sync with configuration backend
			write_config("[{$wgg['pkg_name']}] Tunnel {$tunnel['name']} deleted.");
			
			// Mark any peers as unassigned
			wg_tunnel_unassign_peers($tunnel['name']);

			// Resync the package
			wg_resync();

			// We've got meaningful changes...
			$changes = true;

			// What tunnel would we need to sync to apply these changes?
			$tuns_to_sync[] = $tunnel['name'];

		}

	}

	return array('input_errors' => $input_errors, 'changes' => $changes, 'tuns_to_sync' => $tuns_to_sync);

}

function wg_tunnel_unassign_peers($tunnel_name) {
	global $wgg;

	wg_globals();

	// Assume there is no peers to unassign...
	$changes = false;
	
	foreach (wg_tunnel_get_peers_config($tunnel_name) as [$peer_idx, $peer, $is_new]) {

		$wgg['peers'][$peer_idx]['tun'] = 'unassigned';

		// We've got at least one, so we need to resync with the backend...
		$changes = true;

	}

	if ($changes) {

		// Sync with configuration backend
		write_config("[{$wgg['pkg_name']}] Tunnel {$tunnel_name} peers unassigned.");

	}

}

/*
 * This transforms a raw peer post consisting of repeatables 
 */
function wg_peer_allowedips($post) {

	// Convert the post into proper arrays
	$allowedips = wg_parse_post_repeatables($post, array('address', 'address_subnet', 'address_descr'));

	$ret_array = array();

	// Loop through each address entry
	foreach ($allowedips as $row) {

		// Remove any accidental whitespace
		$row['address'] = trim($row['address']);

		if (!empty($row['address'])) {

			$ret_array[] 	= array(
						'address' 	=> $row['address'], 
						'mask' 		=> $row['address_subnet'], 
						'descr' 	=> $row['address_descr']);

		}

	}

	// Consumers expect an array
	return $ret_array;

}

/*
 * This transforms a raw tunnel post consisting of repeatables 
 */
function wg_tunnel_addresses($post) {

	// Convert the post into proper arrays
	$addresses = wg_parse_post_repeatables($post, array('address', 'address_subnet', 'address_descr'));

	$ret_array = array();

	// Loop through each address entry
	foreach ($addresses as $row) {

		// Remove any accidental whitespace
		$row['address'] = trim($row['address']);

		if (!empty($row['address'])) {

			$ret_array[] 	= array(
						'address' 	=> $row['address'], 
						'mask' 		=> $row['address_subnet'], 
						'descr' 	=> $row['address_descr']);

		}

	}

	// Consumers expect an array
	return $ret_array;

}

/*
 * This converts a raw form post containing any repeatables like addresses, subnets, and descriptions and returns an actual array
 */
function wg_parse_post_repeatables($post, $fields = null) {

	$ret_array = array();

	if (is_array($fields)) {

		foreach ($fields as $field) {

			$x = 0;

			while (!is_null($post["{$field}{$x}"])) {

				$ret_array[$x][$field] = $post["{$field}{$x}"];

				$x++;

			}

		}

	}

	return $ret_array;

}

/*
 * This resolves peer endpoint hostnames per tunnel and updates the kernel accordingly
 * This returns a DateTime object corresponding to the last update time
 */
function wg_resolve_endpoints() {
	global $wgg;

	// Get the latest info
	wg_globals();

	// Do we have any peers?
	if (isset($wgg['peers']) && is_array($wgg['peers'])) {

		foreach ($wgg['peers'] as $peer) {

			if (!empty($peer['endpoint'])) {

				// Strip port if empty
				$peer['port'] = !empty($peer['port']) ? ":{$peer['port']}" : null;

				$endpoint = "{$peer['endpoint']}{$peer['port']}";

				wg_peer_set_endpoint($peer['tun'], $peer['publickey'], $endpoint);

			}

		}

	}

	$last_update_time = new DateTime();

	return $last_update_time;

}

/*
 * Determines if endpoint hostnames should be re-resolved
 * based on the last time the hostnames were re-resolved
 * 
 * An interval of 0 will effectively disable re-resolving
 */
function wg_should_reresolve_endpoints($last_update_time) {
	global $wgg;

	try {

		// Get the current timestamp
		$current_time = (new DateTime)->getTimestamp();

		// Calculate the time for the next update event
		$time_to_update = $last_update_time->getTimestamp() + wg_get_endpoint_resolve_interval();

		// Should we update? A 0 TTL means disabled.
		return (wg_get_endpoint_resolve_interval() > 0 && ($time_to_update <= $current_time));

	} catch (Exception $e) {

		// Something probably went wrong with DateTime, just bail out...
		return false;

	}

}

/*
 * Gets the hostname resolve interval (in seconds)
 * Returns 0 if something is wrong with the configuration
 */
function wg_get_endpoint_resolve_interval() {
	global $config, $wgg;

	// Are we tracking the system value?
	if (isset($wgg['config']['resolve_interval_track']) 
		&& ($wgg['config']['resolve_interval_track'] == 'yes')) {

		if (isset($config['system']['aliasesresolveinterval']) 
			&& is_numeric($config['system']['aliasesresolveinterval'])) {

			return $config['system']['aliasesresolveinterval'];

		}
			
		// The pfSense default interval
		return 300;

	}

	// Looks like we are using our own value...
	if (isset($wgg['config']['resolve_interval'])
		&& is_numericint($wgg['config']['resolve_interval'])) {

		return $wgg['config']['resolve_interval'];

	}

	// Something is wrong, so just return the default...
	return $wgg['default_resolve_interval'];

}

function wg_do_settings_post($post) {
	global $wgg;

	wg_globals();

	$pconfig = $input_errors = array();

	// Assume no changes will be made...
	$changes = false;

	// We need to save the "old config" to compare against later...
	$pconfig = $old_config = $wgg['config'];

	$pconfig['keep_conf'] 			= empty($post['keep_conf']) ? 'no' : $post['keep_conf'];
					
	$pconfig['hide_secrets']		= empty($post['hide_secrets']) ? 'no' : $post['hide_secrets'];

	$pconfig['resolve_interval']		= empty($post['resolve_interval']) && ($post['resolve_interval'] !== '0') ? $wgg['default_resolve_interval'] : $post['resolve_interval'];

	$pconfig['resolve_interval_track']	= empty($post['resolve_interval_track']) ? 'no' : $post['resolve_interval_track'];

	$input_errors = wg_validate_settings_post($pconfig);

	if (empty($input_errors)) {

		$wgg['config'] = $pconfig;

		// Sync with configuration backend
		write_config("[{$wgg['pkg_name']}] Package settings saved.");

		// Resync the package
		wg_resync();

		// Do we have meaningful changes?
		$changes = ($pconfig != $old_config);

	}

	return array('input_errors' => $input_errors, 'changes' => $changes, 'pconfig' => $pconfig);

}

/*
 * Takes a raw post for a peer, validates it, and saves it to the configuration system
 */
function wg_do_peer_post($post) {
	global $wgg;

	wg_globals();

	$pconfig = $input_errors = array();

	// Assume no changes will be made...
	$changes = false;

	[$peer_idx, $pconfig, $is_new] = wg_peer_get_config($post['index'], true);

	// We need to save the "old config" to compare against later...
	$old_pconfig = $pconfig;

	$pconfig['enabled'] 		= empty($post['enabled']) ? 'no' : $post['enabled'];

	$pconfig['tun'] 		= $post['tun'];

	$pconfig['descr'] 		= $post['descr'];

	$pconfig['endpoint'] 		= $post['endpoint'];

	$pconfig['port'] 		= empty($post['port']) ? $wgg['default_port'] : $post['port'];

	$pconfig['persistentkeepalive']	= $post['persistentkeepalive'];

	$pconfig['publickey'] 		= $post['publickey'];
	
	$pconfig['presharedkey']	= $post['presharedkey'];

	$pconfig['allowedips']['row'] 	= wg_peer_allowedips($post);

	// Looks like we have a dynamic endpoint, so clear endpoint or port variables before saving
	if (isset($post['dynamic']) && $post['dynamic'] == 'yes') {

		unset($pconfig['endpoint'], $pconfig['port']);

	}

	$input_errors = wg_validate_peer_post($pconfig, $peer_idx);

	if (empty($input_errors)) {

		$wgg['peers'][$peer_idx] = $pconfig;

		// Sync with configuration backend
		write_config("[{$wgg['pkg_name']}] Peer {$pconfig['descr']} updated.");

		// Resync the package
		wg_resync();

		/*
		 * Figure out what tunnel to sync to make these changes (if any).
		 * 
		 * If the peer is re-unassigned, then $old_pconfig['tun'] needs to be resynced.
		 */
		foreach (array($pconfig['tun'], $old_pconfig['tun']) as $tun) {

			if (wg_is_valid_tunnel($tun)) {

				$changes = ($pconfig != $old_pconfig) || $is_new;

				$tuns_to_sync[] = $tun;

			}

		}

	}

	return array('input_errors' => $input_errors, 'changes' => $changes, 'tuns_to_sync' => $tuns_to_sync, 'pconfig' => $pconfig);

}

/*
 * Takes a raw post for a tunnel, validates it, and saves it to the configuration system
 */
function wg_do_tunnel_post($post) {
	global $wgg;

	wg_globals();

	$pconfig = $input_errors = array();

	// Assume no changes will be made...
	$changes = false;

	[$tun_idx, $pconfig, $is_new] = wg_tunnel_get_config($post['index'], true);

	// We need to save the "old config" to compare against later...
	$old_pconfig = $pconfig;

	$key = wg_gen_publickey($post['privatekey']);

	$pconfig['name']		= empty($pconfig['name']) ? next_wg_if() : $pconfig['name'];

	$pconfig['enabled']		= empty($post['enabled']) ? 'no' : $post['enabled'];

	$pconfig['descr']		= $post['descr'];

	$pconfig['listenport']		= empty($post['listenport']) ? next_wg_port() : $post['listenport'];

	$pconfig['privatekey']		= $key['privkey'];

	$pconfig['publickey']		= $key['pubkey'];

	$pconfig['mtu']			= empty($post['mtu']) ? $wgg['default_mtu'] : $post['mtu'];

	$pconfig['addresses']['row'] 	= wg_tunnel_addresses($post);

	$input_errors			= wg_validate_tunnel_post($pconfig, $tun_idx);

	if (empty($input_errors)) {

		$wgg['tunnels'][$tun_idx] = $pconfig;

		// Sync with configuration backend
		write_config("[{$wgg['pkg_name']}] Tunnel {$pconfig['name']} updated.");

		// Resync the package
		wg_resync();

		// Do we have meaningful changes?
		$changes = ($pconfig != $old_pconfig);

		// What tunnel would we need to sync to apply these changes?
		$tuns_to_sync[] = $pconfig['name'];

	}

	return array('input_errors' => $input_errors, 'changes' => $changes, 'tuns_to_sync' => $tuns_to_sync, 'pconfig' => $pconfig);

}

function wg_apply_list_get($list, $delete_after_get = true) {
	global $wgg;

	$toapplylist = array();

	if (isset($wgg['applylist'][$list])) {

		$listpath = $wgg['applylist'][$list];

		if (file_exists($listpath)) {

			$toapplylist = (array) unserialize(file_get_contents($listpath));

		}

		// Usually just want to delete the apply list after we read it...
		if ($delete_after_get) {

			unlink_if_exists($listpath);

		}

	}

	return $toapplylist;

}

function wg_apply_list_add($list, $entries) {
	global $wgg;

	$toapplylist = array();

	if (isset($wgg['applylist'][$list])) {

		$listpath = $wgg['applylist'][$list];

		// Get the current list without deleting it...
		$toapplylist = wg_apply_list_get($list, false);

		// Need to type cast $entires to array and remove duplicates
		$toapplylist = array_unique(array_merge($toapplylist, (array) $entries));

		file_put_contents($listpath, serialize($toapplylist));

		$toapplylist = unserialize(file_get_contents($listpath));

	}

	return $toapplylist;

}

/*
 * This compares the running vs configured tunnels and destroys any rogues
 */
function wg_destroy_rogue_tunnels(&$cmds = null) {
	global $wgg;

	$ret_code = 0;

	$running_ifs = wg_get_running_ifs();

	$config_ifs = wg_get_configured_ifs();

	$rogue_tunnels = array_diff($running_ifs, $config_ifs);

	foreach ($rogue_tunnels as $tunnel) {

		$cmds[] = $res = wg_ifconfig_if_destroy($tunnel);

		$ret_code = $res['ret_code'];

	}

	return ($ret_code == 0);

}

/*
 * This builds, rebuilds, or destroys tunnel interfaces
 * If $tunnel_names is null, this will apply to all configured tunnel interfaces
 */
function wg_tunnel_sync($tunnel_names = null, $restart_services = false, $resolve_endpoints = true, $json = false) {
	global $wgg;

	// Fetch and build the latest info
	wg_resync();

	// Let's assume everything will be fine
	$ret_code = 0;

	$tunnels = array();

	// We need to destroy any rogue tunnels not created by pfSense
	wg_destroy_rogue_tunnels();

	// This is only necessary if the sevice is running
	if (wg_is_service_running()) {

		$tunnel_names = (is_null($tunnel_names) || !is_array($tunnel_names)) ? wg_get_configured_ifs() : $tunnel_names;

		// Sync each tunnel
		foreach ($tunnel_names as $tunnel_name) {

			// Attempt to sync the tunnel
			$tun_sync_status = wg_tunnel_sync_by_name($tunnel_name, false);

			// Build an aggregated return code
			$ret_code |= $tun_sync_status['ret_code'];

			// Collect the return from each individual tunnel
			$tunnels[] = array(
					'name' 		=> $tun_sync_status['config']['name'],
					'ret_code' 	=> $tun_sync_status['ret_code'], 
					'errors' 	=> $tun_sync_status['errors'],
					'config' 	=> $tun_sync_status['config']);

		}
		
		// Reload the filter
		filter_configure();

		// Handle any extra services that need restarting
		if ($restart_services) {

			wg_restart_extra_services();

		}

		// Handle resolving endpoints
		if ($resolve_endpoints) {

			wg_resolve_endpoints();
			
		}

	}

	$res = array('ret_code' => $ret_code, 'tunnels' => $tunnels);

	return $json ? json_encode($res) : $res;

}

/*
 * This builds or rebuilds a specific tunnel interface by name
 */
function wg_tunnel_sync_by_name($tunnel_name, $json = false) {
	global $wgg;

	// Get the latest info
	wg_globals();

	$cmds = $errors = $tunnel = array();

	// We've got a tunnel that we need to build...
	if ([$tun_idx, $tunnel, $is_new] = wg_tunnel_get_config_by_name($tunnel_name)) {

		// Determine desired state of the tunnel
		$state = (isset($tunnel['enabled']) && $tunnel['enabled'] == 'yes');

		// Create the tunnel interface if it does not yet exist
		if (wg_ifconfig_if_create($tunnel['name'], $cmds)) {

			// Add the tunnel interface to the WireGuard interface group
			wg_interface_update_groups($tunnel['name'], $cmds);

			// Update the addresses on the tunnel interface
			wg_interface_update_addresses($tunnel['name'], $cmds);

			// Toggle the interface accordingly instead of tearing it down completely
			wg_ifconfig_if_updown($tunnel['name'], $state, $cmds);

			// Sync interface with WireGuard wg(8)
			wg_wg_if_sync($tunnel['name'], $cmds);

		}

	}

	$parsed_cmds = wg_parse_cmds_array($cmds);

	// Build the return array
	$res = array(
		'ret_code'	=> $parsed_cmds['ret_code'], 
		'errors'	=> $parsed_cmds['errors'], 
		'cmds'		=> $parsed_cmds['cmds'],
		'config'	=> $tunnel);

	return $json ? json_encode($res, JSON_PRETTY_PRINT) : $res;

}

/*
 * Parses an array of $cmds
 */
function wg_parse_cmds_array($cmds) {

	$ret_code = 0;
	
	$errors = array();

	if (isset($cmds) && is_array($cmds)) {

		foreach($cmds as $cmd) {

			$ret_code |= $cmd['ret_code'];

			// Unwrap and collect any errors
			array_push($errors, ...$cmd['errors']);

		}

	}

	return array('ret_code' => $ret_code, 'errors' => $errors, 'cmds' => $cmds);

}

/*
 * This performs some routine checks just to make sure everything is still in order
 */
function wg_resync() {
	global $g, $wgg;

	// Update globals
	wg_globals();

	// Create WireGuard configuration files
	wg_create_config_files();

	// Not really important if we are installing...
	if (!$g['wireguard_installing']) {

		// Reinstall earlyshellcmd in case it was accidently deleted
		wg_earlyshellcmd_install();

		// Recreate interface group in case it was accidently deleted
		wg_ifgroup_install();

		// Update Unbound ACL by recreating it
		wg_unbound_acl_install();

	}
	
	// We don't want active tunnels when the service isn't running
	if (!wg_is_service_running() &&
		is_module_loaded($wgg['kmod'])) {

		wg_destroy_tunnels();

	}

	// We definitely aren't installing at this point...
	unset($g['wireguard_installing']);

}

/*
 * Returns an array of enabled tunnels
 */
function wg_get_enabled_tunnels() {
	global $wgg;

	wg_globals();

	$ret_tunnels = array();

	foreach ($wgg['tunnels'] as $tunnel) {

		if (isset($tunnel['enabled']) && $tunnel['enabled'] == 'yes') {

			$ret_tunnels[] = $tunnel;

		}

	}

	return $ret_tunnels;

}

/*
 * Creates (or recreates) all WireGuard .conf files based on the current XML configuration
 */
function wg_create_config_files($clean = true) {
	global $wgg;

	wg_globals();

	// Create configuration path if it is missing
	if (!file_exists($wgg['conf_path'])) {

		mkdir($wgg['conf_path'], 0700, true);

	} else {

		chmod($wgg['conf_path'], 0700);

	}

	// We almost always want to just overwrite the old configurations
	if ($clean) {

		wg_delete_config_files();

	}

	// Check if there are any configurations to write to disk
	if (isset($wgg['tunnels']) && is_array($wgg['tunnels'])) {

		foreach ($wgg['tunnels'] as $tunnel) {

			wg_make_tunnel_conf_file($tunnel, false);

		}

	}

}

/* 
 * Remove all wg .conf files from any potential configuration directory
 */
function wg_delete_config_files() {
	global $wgg;

	// Loop through each potential conf path and delete all .conf files
	foreach ($wgg['conf_paths_to_clean'] as $path) {

		unlink_if_exists("{$path}/*.conf");

	}

}

/* 
 * Wrapper to sanitize many variables at once
 * Call as wg_htmlspecialchars($var1, $var2, ... $varX)
 * Vars can be a mix of strings and arrays
 */
function wg_htmlspecialchars(&...$vars) {

	foreach ($vars as &$var) {

		if (isset($var) && is_array($var)) {

			array_walk_recursive($var, function(&$x) {

				$x = htmlspecialchars($x);

			});

			continue;

		}

		$var = htmlspecialchars($var);

	}

}

/* 
 * Wrapper to sanitize many variables at once
 * Call as wg_escapeshellargs($var1, $var2, ... $varX)
 * Vars can be a mix of strings and arrays
 */
function wg_escapeshellarg(&...$vars) {

	foreach ($vars as &$var) {

		if (isset($var) && is_array($var)) {

			array_walk_recursive($var, function(&$x) {

				$x = escapeshellarg($x);

			});

			continue;

		}

		$var = escapeshellarg($var);

	}

}

/* 
 * Removes any configuration xml paths as defined by $wgg['xml_conf_tags']
 */
function wg_remove_config_settings() {
	global $config, $wgg;

	// Loop through each potential conf path and unset it
	foreach ($wgg['xml_paths_to_clean'] as $path) {

		array_unset_value($config, $path);

	}

	// Now write out the new config to disk
	write_config("[{$wgg['pkg_name']}] Package configuration removed");

}

/*
 * Writes a WireGuard configuration file for a given tunnel to the configuration path
 */
function wg_make_tunnel_conf_file($tunnel, $include_endpoint = false) {
	global $wgg;

	$txt = "# This WireGuard config file has been created automatically. Do not edit!\n\n";

	$txt .= "# Description: {$tunnel['descr']}\n";

	// Process Interfaces section
	$txt .= "[Interface]\n";

	if (!empty($tunnel['privatekey'])) {

		$key = wg_gen_publickey($tunnel['privatekey']);

		if (!$key['was_clamped']) {

			$txt .= "# The provided PrivateKey was not pre-clamped by the key generator.\n";

			$txt .= "# PrivateKey = {$key['privkey']}\n";

			$txt .= "# PublicKey = {$key['pubkey']}\n";

		}

		$txt .= "PrivateKey = {$key['privkey_clamped']}\n";

	}

	if (!empty($tunnel['listenport'])) {

		$txt .= "ListenPort = {$tunnel['listenport']}\n";

	}

	$txt .= "\n";

	// Process peers section
	foreach (wg_tunnel_get_peers_config($tunnel['name']) as [$peer_idx, $peer, $is_new]) {

		if (isset($peer['enabled']) && $peer['enabled'] == 'yes') {

			$txt .= "# Peer: {$peer['descr']}\n";

			$txt .= "[Peer]\n";

			if (!empty($peer['publickey'])) {

				$txt .= "PublicKey = {$peer['publickey']}\n";

			}

			if (!empty($peer['presharedkey'])) {

				$txt .= "PresharedKey = {$peer['presharedkey']}\n";

			}

			if (is_array($peer['allowedips']['row']) && count($peer['allowedips']['row']) > 0) {

				$allowedips = implode(', ', array_map(fn($x) => "{$x['address']}/{$x['mask']}", $peer['allowedips']['row']));

				$txt .= "AllowedIPs = {$allowedips}\n";

			}

			if (!empty($peer['endpoint'])) {

				$peer['endpoint'] = is_ipaddrv6($peer['endpoint']) ? "[{$peer['endpoint']}]" : $peer['endpoint'];

				$peer['port'] = empty($peer['port']) ? $wgg['default_port'] : $peer['port'];

				$comment = $include_endpoint ? null : '# ';

				// NOTE: Endpoint needs to be commented out to avoid potential failures caused by DNS
				$txt .= "{$comment}Endpoint = {$peer['endpoint']}:{$peer['port']}\n";

			}

			if (!empty($peer['persistentkeepalive'])) {

				$txt .= "PersistentKeepalive = {$peer['persistentkeepalive']}\n";
			}

			$txt .= "\n";

		}

	}

	$conf_path = "{$wgg['conf_path']}/{$tunnel['name']}.conf";

	$old_mask = umask(077);

	file_put_contents($conf_path, $txt);

	umask($old_mask);

}

function wg_download_tunnel($tunnel_name, $failure_redirect) {
	global $wgg;

	// Fetch and build the latest info
	wg_resync();

	$now = new DateTimeImmutable();

	$stamp = $now->format('YmdHis');

	$conf_path = "{$wgg['conf_path']}/{$tunnel_name}.conf";

	$name = "tunnel-{$tunnel_name}-{$stamp}.conf";

	if (file_exists($conf_path)) {

		send_user_download('file', $conf_path, $name);

	}

	// If something goes wrong, bail out to the failure redirect location
	header("Location: {$failure_redirect}");

}

?>