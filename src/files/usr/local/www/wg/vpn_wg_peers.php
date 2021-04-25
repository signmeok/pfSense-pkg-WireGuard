<?php
/*
 * vpn_wg_peers.php
 *
 * part of pfSense (https://www.pfsense.org)
 * Copyright (c) 2021 Rubicon Communications, LLC (Netgate)
 * Copyright (c) 2021 R. Christian McDonald
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

##|+PRIV
##|*IDENT=page-vpn-wireguard
##|*NAME=VPN: WireGuard
##|*DESCR=Allow access to the 'VPN: WireGuard' page.
##|*MATCH=vpn_wg_peers.php*
##|-PRIV

// pfSense includes
require_once('guiconfig.inc');
require_once('functions.inc');

// WireGuard includes
require_once('wireguard/wg.inc');

global $wgg;

wg_globals();

if ($_POST) {

	if (isset($_POST['tun'])) {

		$tun_id = wg_get_tunnel_id($_POST['tun']);

		if ($_POST['act'] == 'toggle') {

			if (is_wg_tunnel_assigned($wgg['tunnels'][$tun_id]['name'])) {
			
				$input_errors[] = gettext('Cannot toggle a WireGuard tunnel while it is assigned as an interface.');

			} else {

				wg_toggle_tunnel($tun_id);

				header("Location: /wg/vpn_wg_tunnels.php");

			}

		} elseif ($_POST['act'] == 'delete') { 

			if (is_wg_tunnel_assigned($wgg['tunnels'][$tun_id]['name'])) {
		
				$input_errors[] = gettext('Cannot delete a WireGuard tunnel while it is assigned as an interface.');
		
			} else {
		
				wg_delete_tunnel($tun_id);
		
				header("Location: /wg/vpn_wg_tunnels.php");
		
			}

		}

	}

}

$shortcut_section = "wireguard";

$pgtitle = array(gettext("VPN"), gettext("WireGuard"), gettext("Peers"));
$pglinks = array("", "/wg/vpn_wg_tunnels.php", "@self");

$tab_array = array();
$tab_array[] = array(gettext("Tunnels"), false, "/wg/vpn_wg_tunnels.php");
$tab_array[] = array(gettext("Peers"), true, "/wg/vpn_wg_peers.php");
$tab_array[] = array(gettext("Settings"), false, "/wg/vpn_wg_settings.php");
$tab_array[] = array(gettext("Status"), false, "/wg/status_wireguard.php");

include("head.inc");

if ($input_errors) {
	print_input_errors($input_errors);
}

display_top_tabs($tab_array);

?>

<form name="mainform" method="post">
<?php
	if (is_array($wgg['peers']) && count($wgg['peers']) == 0):

		print_info_box(gettext('No WireGuard peers have been configured. Click the "Add Peer" button below to create one.'), 'warning', false);
		
	else:
?>
	<div class="panel panel-default">
		<div class="panel-heading"><h2 class="panel-title"><?=gettext('WireGuard Peers')?></h2></div>
		<div class="panel-body table-responsive">
			<table class="table table-striped table-hover">
				<thead>
					<tr>
						<th><?=gettext("Peer")?></th>
						<th><?=gettext("Tunnel")?></th>
						<th><?=gettext("Description")?></th>
						<th><?=gettext("Public key")?></th>
						<th><?=gettext("Peer Address")?></th>
						<th><?=gettext("Allowed IPs")?></th>
						<th><?=gettext("Endpoint")?></th>
						<th><?=gettext("Port")?></th>
						<th>Actions</th>
					</tr>
				</thead>
				<tbody>
<?php
		foreach ($wgg['peers'] as $peer_id => $peer):

			$entryStatus = ($peer['enabled'] == 'yes') ? 'enabled' : 'disabled';

			$icon_toggle = ($peer['enabled'] == 'yes') ? 'ban' : 'check-square-o';	

?>
					<tr ondblclick="document.location='vpn_wg_peers_edit.php?peer=';" class="<?=$entryStatus?>">
						<td><?=htmlspecialchars($peer_id)?></td>
						<td><?=htmlspecialchars($peer['tun'])?></td>
						<td><?=htmlspecialchars($peer['descr'])?></td>
						<td><?=htmlspecialchars(substr($peer['publickey'],0,12).'...')?></td>
						<td><?=htmlspecialchars($peer['peeraddresses']['item'][0])?></td>
						<td><?=htmlspecialchars($peer['allowedips']['item'][0])?></td>
						<td><?=htmlspecialchars($peer['endpoint'])?></td>
						<td><?=htmlspecialchars($peer['port'])?></td>
						<td style="cursor: pointer;">
							<a class="fa fa-pencil" title="<?=gettext("Edit peer")?>" href="<?="vpn_wg_peers_edit.php?peer={$peer_id}"?>"></a>
							<a class="fa fa-<?=$icon_toggle?>" title="<?=gettext("Click to toggle enabled/disabled status")?>" href="<?="?act=toggle&peer={$peer_id}"?>" usepost></a>
							<a class="fa fa-trash text-danger" title="<?=gettext('Delete peer')?>" href="<?="?act=delete&peer={$peer_id}"?>" usepost></a>
						</td>
					</tr>

<?php
		endforeach;
?>
				</tbody>
			</table>
		</div>
	</div>


<?php
	endif;
?>

	<nav class="action-buttons">
		<a href="vpn_wg_peers_edit.php" class="btn btn-success btn-sm">
			<i class="fa fa-plus icon-embed-btn"></i>
			<?=gettext("Add Peer")?>
		</a>
	</nav>
</form>

<script type="text/javascript">
//<![CDATA[

events.push(function() {

});
//]]>
</script>

<?php
include("foot.inc");
?>