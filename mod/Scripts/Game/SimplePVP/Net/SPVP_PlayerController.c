//------------------------------------------------------------------------------------------------
// SimplePVP — Messages et téléportation, du serveur vers un joueur
// SPVP_Notify s'appelle côté serveur ; le contrôleur du joueur exécute chez lui (RPC au propriétaire).
// Livraisons 1, 3 et 4
//------------------------------------------------------------------------------------------------

class SPVP_Notify
{
	//------------------------------------------------------------------------------------------------
	//! Serveur → joueur : encart à l'écran (titre + texte) pendant quelques secondes
	static void ToPlayer(int playerId, string titre, string texte, float duree = 5)
	{
		if (!Replication.IsServer())
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (pc)
			pc.SPVP_Hint(titre, texte, duree);
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur → joueur : téléportation de son personnage (faite chez lui, qui pilote ses mouvements)
	static void Teleport(int playerId, vector position)
	{
		if (!Replication.IsServer())
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(playerId));
		if (pc)
			pc.SPVP_Teleport(position);
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_PlayerController
{
	//------------------------------------------------------------------------------------------------
	void SPVP_Hint(string titre, string texte, float duree)
	{
		Rpc(RpcDo_SPVPHint, titre, texte, duree);
	}

	//------------------------------------------------------------------------------------------------
	void SPVP_Teleport(vector position)
	{
		Rpc(RpcDo_SPVPTeleport, position);
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur → joueur : écran de mort (D01, D16, D17)
	void SPVP_ShowDeath(string contenu, vector corps, vector direction, int delai)
	{
		Rpc(RpcDo_SPVPShowDeath, contenu, corps, direction, delai);
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur → joueur : réapparu, l'écran de mort se ferme
	void SPVP_EndDeath()
	{
		Rpc(RpcDo_SPVPEndDeath);
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur → joueur : sa balle a touché (C25). Peu importe si un message se perd : non fiable, plus rapide.
	void SPVP_HitMarker(bool kill)
	{
		Rpc(RpcDo_SPVPHitMarker, kill);
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur → joueur : kill ou assistance confirmés, lignes de points du popup (P24)
	void SPVP_KillConfirm(array<int> types, array<int> valeurs, string victime, bool croixRouge)
	{
		Rpc(RpcDo_SPVPKillConfirm, types, valeurs, victime, croixRouge);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Unreliable, RplRcver.Owner)]
	protected void RpcDo_SPVPHitMarker(bool kill)
	{
		SPVP_HudComponent hud = SPVP_HudComponent.GetInstance();
		if (hud)
			hud.ShowHitMarker(kill);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_SPVPKillConfirm(array<int> types, array<int> valeurs, string victime, bool croixRouge)
	{
		SPVP_HudComponent hud = SPVP_HudComponent.GetInstance();
		if (!hud)
			return;

		if (croixRouge)
			hud.ShowHitMarker(true);
		hud.AddPoints(types, valeurs, victime);
	}

	//------------------------------------------------------------------------------------------------
	//! Joueur → serveur : choix fait dans un menu SimplePVP
	void SPVP_SendChoice(string kind, string key)
	{
		Rpc(RpcAsk_SPVPChoice, kind, key);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_SPVPShowDeath(string contenu, vector corps, vector direction, int delai)
	{
		SPVP_DeathScreen.Start(contenu, corps, direction, delai);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_SPVPEndDeath()
	{
		SPVP_DeathScreen.Stop();
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Server)]
	protected void RpcAsk_SPVPChoice(string kind, string key)
	{
		if (kind == "mort")
		{
			SPVP_SpawnLogic logic = SPVP_SpawnLogic.GetInstance();
			if (logic)
				logic.OnDeathChoice(GetPlayerId(), key);
		}
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_SPVPHint(string titre, string texte, float duree)
	{
		SCR_HintManagerComponent.ShowCustomHint(texte, titre, duree, true);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Owner)]
	protected void RpcDo_SPVPTeleport(vector position)
	{
		SCR_Global.TeleportLocalPlayer(position);
	}
}
