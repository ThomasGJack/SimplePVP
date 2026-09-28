//------------------------------------------------------------------------------------------------
// SimplePVP — Choix du point d'apparition en ville (D05 à D09), joueurs et bots
// Candidats : des points au sol tirés au hasard dans le cercle (le jeu vérifie qu'un soldat y tient debout, hors de
// l'eau), plus les points posés à la main aux étages et sur les toits (SPVP_SpawnPoint, D08).
// On garde le plus éloigné des combattants vivants ; un point près d'un tir ou d'une mort récente (40 m, 30 s)
// n'est pris qu'en dernier recours (D09). 150 m voulus avec peu de monde, 60 m quand la ville est pleine (D07).
// Livraisons 1 et 3
//------------------------------------------------------------------------------------------------

class SPVP_SpawnPicker
{
	protected static const int ESSAIS = 24;			// points tirés au hasard
	protected static const float MARGE_BORD = 25;	// on n'apparaît pas au ras de la limite
	protected static const float RAYON_RECHERCHE = 12;	// rayon où le jeu cherche une place dégagée autour du point tiré

	//------------------------------------------------------------------------------------------------
	//! Point d'apparition en ville pour ce combattant, et son orientation (vers le centre)
	static vector PickInCity(int fighterId, out vector angles)
	{
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		vector center = rounds.GetCentre();
		float rayon = Math.Max(rounds.GetRayon() - MARGE_BORD, 20);

		array<vector> combattants = {};
		GetFighters(fighterId, combattants);

		// D07 : 150 m avec peu de combattants vivants, jusqu'à 60 m quand la ville est pleine (réglage = minimum)
		float objectif = Math.Max(150 - combattants.Count() * 6, SPVP_Settings.Get().m_fDistanceEnnemi);

		array<vector> candidats = {};
		SPVP_SpawnPoint.GetInCircle(center, rayon, candidats);
		for (int i = 0; i < ESSAIS; i++)
		{
			float angle = Math.RandomFloat(0, Math.PI2);
			float distance = rayon * Math.Sqrt(Math.RandomFloat(0, 1));	// réparti sur toute la surface
			vector tire = center;
			tire[0] = center[0] + Math.Cos(angle) * distance;
			tire[2] = center[2] + Math.Sin(angle) * distance;
			tire[1] = SCR_TerrainHelper.GetTerrainY(tire) + 1;

			vector place;
			if (SCR_WorldTools.FindEmptyTerrainPosition(place, tire, RAYON_RECHERCHE))
				candidats.Insert(place);
		}

		vector meilleur = center;
		float meilleurScore = -1;
		bool trouve = false;
		foreach (vector candidat : candidats)
		{
			float score = NearestDistance(candidat, combattants);
			if (SPVP_Combat.IsDangerous(candidat))
				score = score * 0.1;	// dernier recours

			if (score > meilleurScore)
			{
				meilleurScore = score;
				meilleur = candidat;
				trouve = true;
			}
		}

		if (!trouve)
		{
			SPVP_Log.Warn("Aucune place dégagée trouvée en ville, apparition au centre");
			SCR_WorldTools.FindEmptyTerrainPosition(meilleur, center, 30);
		}
		else if (meilleurScore < objectif && !combattants.IsEmpty())
		{
			SPVP_Log.Info(string.Format("Apparition à %1 m du combattant le plus proche (objectif %2 m)", Math.Round(meilleurScore), Math.Round(objectif)));
		}

		// Regard vers le centre de la ville
		vector direction = center - meilleur;
		direction[1] = 0;
		float cap = Math.Atan2(direction[0], direction[2]) * Math.RAD2DEG;
		angles = Vector(0, cap, 0);
		return meilleur;
	}

	//------------------------------------------------------------------------------------------------
	//! Positions des autres combattants vivants en ville (joueurs et bots)
	protected static void GetFighters(int fighterId, notnull array<vector> outPositions)
	{
		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (info.m_iPlayerId == fighterId || info.m_eLieu != SPVP_ELieu.VILLE)
				continue;

			IEntity character = SPVP_Players.GetAliveEntity(info);
			if (character)
				outPositions.Insert(character.GetOrigin());
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static float NearestDistance(vector position, notnull array<vector> others)
	{
		if (others.IsEmpty())
			return 100000;

		float best = float.MAX;
		foreach (vector other : others)
		{
			float d = vector.DistanceXZ(position, other);
			if (d < best)
				best = d;
		}
		return best;
	}
}
