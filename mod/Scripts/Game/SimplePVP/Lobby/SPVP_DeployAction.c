//------------------------------------------------------------------------------------------------
// SimplePVP — Actions de l'ordinateur du lobby
// « Déployer » (L10) : part tout de suite en ville pendant une manche (L13) ; entre deux manches, ou dans les
// 30 dernières secondes, met en file pour le départ groupé de la manche suivante (R3, L07).
// « Voter : ville » (V16, V17) : une action par ville proposée, pendant le vote.
// Le panneau plein écran (carte, joueurs, kits) arrive avec l'écran de mort (livraison 3).
// Livraisons 1 et 2
//------------------------------------------------------------------------------------------------

class SPVP_DeployAction : ScriptedUserAction
{
	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!Replication.IsServer())
			return;

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(pUserEntity);
		if (playerId <= 0)
			return;

		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		SPVP_SpawnLogic logic = SPVP_SpawnLogic.GetInstance();
		if (!rounds || !logic)
			return;

		SPVP_PlayerInfo info = SPVP_Players.Get(playerId);
		if (info.m_eLieu != SPVP_ELieu.LOBBY || info.m_bApparitionPrevue)
			return;

		string raison;
		int reponse = rounds.CanDeployFromLobby(raison);
		if (reponse == SPVP_EDeploiement.REFUSE)
		{
			SPVP_Notify.ToPlayer(playerId, "Déploiement impossible", raison, 4);
			return;
		}

		if (reponse == SPVP_EDeploiement.FILE)
		{
			info.m_bEnFile = !info.m_bEnFile;
			if (info.m_bEnFile)
				SPVP_Notify.ToPlayer(playerId, "En file", raison + ". Refais l'action pour annuler.", 5);
			else
				SPVP_Notify.ToPlayer(playerId, "Départ annulé", "Tu restes au lobby.", 3);
			return;
		}

		SPVP_Log.Info(GetGame().GetPlayerManager().GetPlayerName(playerId) + " se déploie à " + rounds.GetVille());

		// Hors de l'action : le personnage qui fait l'action va être remplacé
		info.m_bEnFile = false;
		info.m_bApparitionPrevue = true;
		GetGame().GetCallqueue().CallLater(logic.Transfer, 100, false, playerId, SPVP_ELieu.VILLE);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (!rounds)
			return false;

		string raison;
		int reponse = rounds.CanDeployFromLobby(raison);
		if (reponse == SPVP_EDeploiement.MAINTENANT)
		{
			outName = string.Format("DÉPLOYER — %1 (%2)", rounds.GetVille(), SPVP_RoundManagerComponent.FormatTemps(rounds.GetRestant()));
			return true;
		}

		// En file ou pas : le texte est le même, l'action bascule (le serveur le confirme par un message)
		outName = "DÉPLOYER à la prochaine manche (ou annuler)";
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBePerformedScript(IEntity user)
	{
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (!rounds)
			return false;

		string raison;
		if (rounds.CanDeployFromLobby(raison) == SPVP_EDeploiement.REFUSE)
		{
			SetCannotPerformReason(raison);
			return false;
		}
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return true;
	}
}

//------------------------------------------------------------------------------------------------
class SPVP_VoteAction : ScriptedUserAction
{
	[Attribute("0", UIWidgets.EditBox, "Numéro de la ville proposée (0, 1 ou 2)")]
	protected int m_iChoix;

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!Replication.IsServer())
			return;

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(pUserEntity);
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (playerId <= 0 || !rounds)
			return;

		rounds.Vote(playerId, m_iChoix);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (!rounds)
			return false;

		outName = string.Format("Voter : %1 (%2 voix)", rounds.GetVoteNom(m_iChoix), rounds.GetVoteVoix(m_iChoix));
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		return rounds && rounds.GetEtat() == SPVP_EEtat.VOTE && m_iChoix < rounds.GetVoteCount();
	}
}

//------------------------------------------------------------------------------------------------
//! A08 : « Kit prêt : Assaut » — le personnage du lobby est recréé avec ce kit (modifiable ensuite à l'arsenal)
class SPVP_KitAction : ScriptedUserAction
{
	[Attribute("0", UIWidgets.EditBox, "Numéro du kit prêt (0 à 5)")]
	protected int m_iKit;

	//------------------------------------------------------------------------------------------------
	override void PerformAction(IEntity pOwnerEntity, IEntity pUserEntity)
	{
		if (!Replication.IsServer())
			return;

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(pUserEntity);
		SPVP_SpawnLogic logic = SPVP_SpawnLogic.GetInstance();
		if (playerId <= 0 || !logic)
			return;

		// Hors de l'action : le personnage qui fait l'action va être remplacé
		GetGame().GetCallqueue().CallLater(logic.TakePreset, 100, false, playerId, m_iKit);
	}

	//------------------------------------------------------------------------------------------------
	override bool GetActionNameScript(out string outName)
	{
		outName = "Kit prêt : " + SPVP_KitNames.Get(m_iKit);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanBeShownScript(IEntity user)
	{
		return !SPVP_KitNames.Get(m_iKit).IsEmpty();
	}
}

//------------------------------------------------------------------------------------------------
//! Noms des kits prêts, connus aussi chez les joueurs (lus sur le mode de jeu, pas depuis le serveur)
class SPVP_KitNames
{
	static string Get(int index)
	{
		SPVP_SpawnLogic logic = SPVP_SpawnLogic.GetInstance();
		if (!logic)
			return "";
		return logic.GetPresetName(index);
	}
}
