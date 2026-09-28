//------------------------------------------------------------------------------------------------
// SimplePVP — Lobby (zone sûre)
// Le point d'arrivée est l'entité nommée SPVP_LobbyArrivee (calque Lobby du monde) : le joueur y apparaît
// toujours, tourné dans le sens de la flèche (L21). Dans le rayon du lobby, personne ne prend de dégâts (L04) ;
// celui qui sort à pied est ramené au point d'arrivée (L05).
// Livraison 1
//------------------------------------------------------------------------------------------------

class SPVP_Lobby
{
	static const string ARRIVEE = "SPVP_LobbyArrivee";
	static const float ECART_ARRIVEE = 2.0;		// écart au hasard autour du point, pour ne pas empiler les soldats

	//------------------------------------------------------------------------------------------------
	//! Point d'arrivée du lobby et son cap (degrés)
	static bool GetArrivee(out vector position, out float cap)
	{
		IEntity marker = GetGame().GetWorld().FindEntityByName(ARRIVEE);
		if (!marker)
		{
			// Repli : base de Levie (ancien point de spawn de la PAG)
			position = Vector(7414.731, 162.369, 4333.72);
			cap = -64.838;
			return false;
		}

		position = marker.GetOrigin();
		cap = marker.GetAngles()[1];
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! Point d'apparition au lobby : le point d'arrivée, à ECART_ARRIVEE près, même cap
	static vector GetSpawnPosition(out vector angles)
	{
		vector position;
		float cap;
		if (!GetArrivee(position, cap))
			SPVP_Log.Warn("Entité " + ARRIVEE + " introuvable dans le monde : point de repli utilisé (base de Levie)");

		float angle = Math.RandomFloat(0, Math.PI2);
		float distance = Math.RandomFloat(0, ECART_ARRIVEE);
		position[0] = position[0] + Math.Cos(angle) * distance;
		position[2] = position[2] + Math.Sin(angle) * distance;

		angles = Vector(0, cap, 0);
		return position;
	}

	//------------------------------------------------------------------------------------------------
	//! Position dans le rayon du lobby ?
	static bool IsInLobby(vector position)
	{
		vector center;
		float cap;
		GetArrivee(center, cap);

		float rayon = 40;
		if (Replication.IsServer())
			rayon = SPVP_Settings.Get().m_fRayonLobby;

		return vector.DistanceSqXZ(position, center) <= rayon * rayon;
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur, chaque seconde : un joueur du lobby sorti à pied est ramené au point d'arrivée (L05)
	static void GuardLobby()
	{
		vector center;
		float cap;
		GetArrivee(center, cap);
		float rayon = SPVP_Settings.Get().m_fRayonLobby;

		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (info.m_eLieu != SPVP_ELieu.LOBBY)
				continue;

			IEntity character = SPVP_Players.GetAliveCharacter(info.m_iPlayerId);
			if (!character)
				continue;

			if (vector.DistanceSqXZ(character.GetOrigin(), center) <= rayon * rayon)
				continue;

			vector angles;
			vector position = GetSpawnPosition(angles);
			SPVP_Notify.Teleport(info.m_iPlayerId, position);
			SPVP_Notify.ToPlayer(info.m_iPlayerId, "Lobby", "Utilise l'ordinateur pour te déployer en ville.", 4);
		}
	}
}
