//------------------------------------------------------------------------------------------------
// SimplePVP — Règle de dégâts (serveur)
// Aucun dégât (balles, explosions, chutes, saignement) pour :
//   - un joueur qui n'est pas en ville (lobby, en transit) — L04 ;
//   - tout le monde quand la manche n'est pas en cours (fin de manche, pause) — P06.
// - la protection d'apparition : 10 % des dégâts pendant 5 s (D12-D14, voir SPVP_Combat).
// - chaque coup est noté pour le score (SPVP_HitTracker).
// Livraisons 1, 3 et 4
//------------------------------------------------------------------------------------------------

modded class SCR_CharacterDamageManagerComponent
{
	//------------------------------------------------------------------------------------------------
	override bool HijackDamageHandling(notnull BaseDamageContext damageContext)
	{
		if (Replication.IsServer())
		{
			if (SPVP_IsProtected())
				return true;	// dégât ignoré

			// D12, D13 : protection d'apparition, 10 % des dégâts
			damageContext.damageValue = damageContext.damageValue * SPVP_Combat.GetDamageFactor(GetOwner());

			// Qui a touché (kills achevés, assistances, headshots, hit markers) : livraison 4
			SPVP_HitTracker.Record(GetOwner(), damageContext);
		}

		return super.HijackDamageHandling(damageContext);
	}

	//------------------------------------------------------------------------------------------------
	protected bool SPVP_IsProtected()
	{
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (!rounds)
			return false;

		IEntity owner = GetOwner();
		if (!owner)
			return false;

		// Manche arrêtée : plus personne ne peut mourir
		if (rounds.GetEtat() != SPVP_EEtat.EN_COURS)
			return true;

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(owner);
		if (playerId <= 0)
			return false;	// pas un joueur (bots : livraison 5)

		SPVP_PlayerInfo info = SPVP_Players.Find(playerId);
		if (!info)
			return true;

		return info.m_eLieu != SPVP_ELieu.VILLE;
	}
}
