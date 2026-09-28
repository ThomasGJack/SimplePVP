//------------------------------------------------------------------------------------------------
// SimplePVP — Réglages du serveur
// Fichier : $profile:SimplePVP/reglages.json (profil du serveur), lu au démarrage, serveur seulement.
// S'il manque, il est créé avec les valeurs par défaut. Un réglage absent garde sa valeur par défaut.
// Les villes sont écrites à plat : villes_nombre, puis ville_1_nom, ville_1_x, ville_1_z, ville_1_rayon…
// Livraisons 1, bots, 2 et 4
//------------------------------------------------------------------------------------------------

class SPVP_Settings
{
	static const string FOLDER = "$profile:SimplePVP";
	static const string PATH = "$profile:SimplePVP/reglages.json";

	// --- Manches (P03, P07, L07) : 15 min, puis 10 s de résultats + 15 s de vote + 5 s de départ figé = 30 s
	int m_iDureeManche = 900;
	int m_iDureeResultats = 10;
	int m_iDureeVote = 15;
	int m_iDureeDepart = 5;
	int m_iBlocageFin = 30;				// déploiement depuis le lobby refusé dans les dernières secondes

	// --- Villes (V09, V18)
	int m_iSeuilGrandeZone = 16;		// vrais joueurs connectés à partir desquels la grande zone est utilisée

	// --- Apparition (D10)
	int m_iDelaiReapparition = 5;		// secondes entre la mort et la réapparition en ville
	float m_fDistanceEnnemi = 60;		// distance visée entre un point d'apparition et un combattant vivant

	// --- Zone (V13, V14)
	int m_iTempsHorsZone = 10;			// secondes pour revenir dans la zone

	// --- Lobby (L03, L05)
	float m_fRayonLobby = 40;			// au-delà, le joueur est ramené au point d'arrivée

	// --- Monde (V21 à V23, C19, AN03)
	int m_iNuitChance = 6;				// environ 1 manche sur N de nuit (0 = jamais), jamais deux de suite
	bool m_bEclaircirNuit = true;		// éclaircissement de nuit du jeu pendant les manches de nuit
	bool m_bOuvrirPortes = true;		// portes de la ville ouvertes au début de chaque manche
	int m_iCorpsMax = 30;				// au-delà, les corps les plus anciens disparaissent

	// --- Verrou du mod (S23) : clé du serveur officiel, vérifiée sur serveur dédié seulement
	string m_sCleServeur;

	// --- Bots (I03 à I17)
	bool m_bBotsActifs = true;
	int m_iBotsMax = 10;					// I05 : jamais plus
	int m_iBotsJoueursFictifs = 0;			// essais seul : ajoute des joueurs imaginaires au calcul des paliers
	bool m_bBotsAttendreDeploiement = true;	// I08 : pas de bots tant qu'aucun vrai joueur n'est déployé dans la manche
	int m_iPalier1Joueurs = 3;				// de 1 à 3 joueurs…
	int m_iPalier1Bots = 10;				// …10 bots
	int m_iPalier2Joueurs = 7;
	int m_iPalier2Bots = 6;
	int m_iPalier3Joueurs = 11;
	int m_iPalier3Bots = 3;					// au-delà : aucun bot (I04)
	float m_fBotsPrecisionErreur = 0.7;		// I12 : erreur de visée (CRX : 1,0 vétéran, 0,7 expert, 0 = tir parfait)
	float m_fBotsPorteeTir = 300;			// I17 : distance maximale de tir
	int m_iBotsIndiceMin = 60;				// I14 : secteur vague toutes les 60 à 120 s
	int m_iBotsIndiceMax = 120;
	float m_fBotsIndiceRayon = 50;			// secteur d'environ 100 m

	// --- Score (P01, P10 à P19, PN05, PN06) : livraison 4
	int m_iPointsKill = 100;
	int m_iBonusTete = 50;
	int m_iBonusDistance = 50;
	float m_fDistanceBonus = 150;			// mètres
	int m_iBonusAssistance = 50;
	float m_fFenetreAssistance = 10;		// secondes
	int m_iPrimeLeader = 100;				// tuer le 1er (vrai joueur, au moins 1 kill)
	float m_fFenetreAttribution = 30;		// mort seule créditée au dernier tireur (P11)
	float m_fFenetreDeconnexion = 10;		// départ en pleine blessure (PN06)
	int m_iReconnexionMin = 5;				// score gardé si le joueur revient à temps (PN05)
	int m_iKillsMinVictoire = 1;			// sinon « Pas de vainqueur »
	bool m_bSerieSoin = true;				// P18 : 3, 5 et 10 kills sans mourir
	bool m_bSerieMunitions = true;
	bool m_bSerieBots = true;				// les bots aussi (sans bandeau)

	// --- Villes de la rotation
	ref array<ref SPVP_Arena> m_aVilles = {};

	protected static ref SPVP_Settings s_Instance;

	//------------------------------------------------------------------------------------------------
	static SPVP_Settings Get()
	{
		if (!s_Instance)
		{
			s_Instance = new SPVP_Settings();
			s_Instance.Load();
		}
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	//! Oubli des réglages en mémoire (nouveau lancement dans Workbench : les variables statiques survivent)
	static void Reset()
	{
		s_Instance = null;
	}

	//------------------------------------------------------------------------------------------------
	protected void Load()
	{
		FileIO.MakeDirectory(FOLDER);

		JsonLoadContext ctx = new JsonLoadContext();
		if (!FileIO.FileExists(PATH))
		{
			SetDefaultCities();
			Save();
			SPVP_Log.Info("Réglages : fichier créé avec les valeurs par défaut (" + PATH + ")");
			return;
		}

		if (!ctx.LoadFromFile(PATH))
		{
			SPVP_Log.Error("Réglages : " + PATH + " illisible (JSON invalide) : valeurs par défaut utilisées, fichier NON écrasé");
			SetDefaultCities();
			return;
		}

		ReadInt(ctx, "manche_duree_s", m_iDureeManche, 60, 7200);
		ReadInt(ctx, "manche_resultats_s", m_iDureeResultats, 3, 120);
		ReadInt(ctx, "manche_vote_s", m_iDureeVote, 5, 120);
		ReadInt(ctx, "manche_depart_s", m_iDureeDepart, 0, 30);
		ReadInt(ctx, "manche_blocage_fin_s", m_iBlocageFin, 0, 600);
		ReadInt(ctx, "ville_seuil_grande_zone", m_iSeuilGrandeZone, 1, 128);
		ReadInt(ctx, "reapparition_delai_s", m_iDelaiReapparition, 1, 60);
		ReadFloat(ctx, "reapparition_distance_ennemi_m", m_fDistanceEnnemi, 0, 500);
		ReadInt(ctx, "zone_temps_hors_zone_s", m_iTempsHorsZone, 1, 120);
		ReadFloat(ctx, "lobby_rayon_m", m_fRayonLobby, 5, 500);
		ReadInt(ctx, "monde_nuit_une_sur", m_iNuitChance, 0, 100);
		ReadBool(ctx, "monde_eclaircir_nuit", m_bEclaircirNuit);
		ReadBool(ctx, "monde_ouvrir_portes", m_bOuvrirPortes);
		ReadInt(ctx, "monde_corps_max", m_iCorpsMax, 5, 200);

		string cle;
		if (ctx.ReadValue("cle_serveur", cle))
			m_sCleServeur = cle;

		ReadBool(ctx, "bots_actifs", m_bBotsActifs);
		ReadBool(ctx, "bots_attendre_premier_deploiement", m_bBotsAttendreDeploiement);
		ReadInt(ctx, "bots_max", m_iBotsMax, 0, 30);
		ReadInt(ctx, "bots_joueurs_fictifs", m_iBotsJoueursFictifs, 0, 64);
		ReadInt(ctx, "bots_palier1_joueurs_max", m_iPalier1Joueurs, 1, 64);
		ReadInt(ctx, "bots_palier1_bots", m_iPalier1Bots, 0, 30);
		ReadInt(ctx, "bots_palier2_joueurs_max", m_iPalier2Joueurs, 1, 64);
		ReadInt(ctx, "bots_palier2_bots", m_iPalier2Bots, 0, 30);
		ReadInt(ctx, "bots_palier3_joueurs_max", m_iPalier3Joueurs, 1, 64);
		ReadInt(ctx, "bots_palier3_bots", m_iPalier3Bots, 0, 30);
		ReadFloat(ctx, "bots_precision_erreur", m_fBotsPrecisionErreur, 0, 3);
		ReadFloat(ctx, "bots_portee_tir_m", m_fBotsPorteeTir, 50, 2000);
		ReadInt(ctx, "bots_indice_min_s", m_iBotsIndiceMin, 10, 1200);
		ReadInt(ctx, "bots_indice_max_s", m_iBotsIndiceMax, 10, 1200);
		ReadFloat(ctx, "bots_indice_rayon_m", m_fBotsIndiceRayon, 0, 300);

		ReadInt(ctx, "score_points_kill", m_iPointsKill, 0, 10000);
		ReadInt(ctx, "score_bonus_tete", m_iBonusTete, 0, 10000);
		ReadInt(ctx, "score_bonus_distance", m_iBonusDistance, 0, 10000);
		ReadFloat(ctx, "score_distance_bonus_m", m_fDistanceBonus, 10, 3000);
		ReadInt(ctx, "score_bonus_assistance", m_iBonusAssistance, 0, 10000);
		ReadFloat(ctx, "score_fenetre_assistance_s", m_fFenetreAssistance, 0, 120);
		ReadInt(ctx, "score_prime_leader", m_iPrimeLeader, 0, 10000);
		ReadFloat(ctx, "score_fenetre_attribution_s", m_fFenetreAttribution, 0, 300);
		ReadFloat(ctx, "score_fenetre_deconnexion_s", m_fFenetreDeconnexion, 0, 300);
		ReadInt(ctx, "score_reconnexion_min", m_iReconnexionMin, 0, 60);
		ReadInt(ctx, "score_kills_min_victoire", m_iKillsMinVictoire, 0, 1000);
		ReadBool(ctx, "score_serie_soin", m_bSerieSoin);
		ReadBool(ctx, "score_serie_munitions", m_bSerieMunitions);
		ReadBool(ctx, "score_serie_bots", m_bSerieBots);

		int count;
		if (ctx.ReadValue("villes_nombre", count))
		{
			for (int i = 1; i <= count; i++)
			{
				string prefix = "ville_" + i.ToString() + "_";
				string nom;
				float x, z;
				if (!ctx.ReadValue(prefix + "nom", nom) || !ctx.ReadValue(prefix + "x", x) || !ctx.ReadValue(prefix + "z", z))
				{
					SPVP_Log.Warn("Réglages : ville " + i.ToString() + " incomplète (nom, x, z), ignorée");
					continue;
				}

				SPVP_Arena arena = new SPVP_Arena(nom, x, z);
				ReadFloat(ctx, prefix + "rayon", arena.m_fRayonGrand, 30, 1000);
				arena.m_fRayonPetit = arena.m_fRayonGrand;
				ReadFloat(ctx, prefix + "rayon_petit", arena.m_fRayonPetit, 30, 1000);
				ReadInt(ctx, prefix + "joueurs_min", arena.m_iJoueursMin, 0, 128);
				ReadInt(ctx, prefix + "joueurs_max", arena.m_iJoueursMax, 0, 128);
				string groupe;
				if (ctx.ReadValue(prefix + "groupe", groupe))
					arena.m_sGroupe = groupe;
				ReadBool(ctx, prefix + "actif", arena.m_bActif);
				m_aVilles.Insert(arena);
			}
		}

		if (m_aVilles.IsEmpty())
		{
			SPVP_Log.Warn("Réglages : aucune ville lue, villes par défaut utilisées");
			SetDefaultCities();
		}

		SPVP_Log.Info(string.Format("Réglages chargés : manche %1 s, %2 ville(s)", m_iDureeManche, m_aVilles.Count()));
	}

	//------------------------------------------------------------------------------------------------
	protected void Save()
	{
		JsonSaveContext ctx = new JsonSaveContext();
		ctx.WriteValue("manche_duree_s", m_iDureeManche);
		ctx.WriteValue("manche_resultats_s", m_iDureeResultats);
		ctx.WriteValue("manche_vote_s", m_iDureeVote);
		ctx.WriteValue("manche_depart_s", m_iDureeDepart);
		ctx.WriteValue("manche_blocage_fin_s", m_iBlocageFin);
		ctx.WriteValue("ville_seuil_grande_zone", m_iSeuilGrandeZone);
		ctx.WriteValue("reapparition_delai_s", m_iDelaiReapparition);
		ctx.WriteValue("reapparition_distance_ennemi_m", m_fDistanceEnnemi);
		ctx.WriteValue("zone_temps_hors_zone_s", m_iTempsHorsZone);
		ctx.WriteValue("lobby_rayon_m", m_fRayonLobby);
		ctx.WriteValue("monde_nuit_une_sur", m_iNuitChance);
		ctx.WriteValue("monde_eclaircir_nuit", m_bEclaircirNuit);
		ctx.WriteValue("monde_ouvrir_portes", m_bOuvrirPortes);
		ctx.WriteValue("monde_corps_max", m_iCorpsMax);
		ctx.WriteValue("cle_serveur", m_sCleServeur);

		ctx.WriteValue("bots_actifs", m_bBotsActifs);
		ctx.WriteValue("bots_attendre_premier_deploiement", m_bBotsAttendreDeploiement);
		ctx.WriteValue("bots_max", m_iBotsMax);
		ctx.WriteValue("bots_joueurs_fictifs", m_iBotsJoueursFictifs);
		ctx.WriteValue("bots_palier1_joueurs_max", m_iPalier1Joueurs);
		ctx.WriteValue("bots_palier1_bots", m_iPalier1Bots);
		ctx.WriteValue("bots_palier2_joueurs_max", m_iPalier2Joueurs);
		ctx.WriteValue("bots_palier2_bots", m_iPalier2Bots);
		ctx.WriteValue("bots_palier3_joueurs_max", m_iPalier3Joueurs);
		ctx.WriteValue("bots_palier3_bots", m_iPalier3Bots);
		ctx.WriteValue("bots_precision_erreur", m_fBotsPrecisionErreur);
		ctx.WriteValue("bots_portee_tir_m", m_fBotsPorteeTir);
		ctx.WriteValue("bots_indice_min_s", m_iBotsIndiceMin);
		ctx.WriteValue("bots_indice_max_s", m_iBotsIndiceMax);
		ctx.WriteValue("bots_indice_rayon_m", m_fBotsIndiceRayon);

		ctx.WriteValue("score_points_kill", m_iPointsKill);
		ctx.WriteValue("score_bonus_tete", m_iBonusTete);
		ctx.WriteValue("score_bonus_distance", m_iBonusDistance);
		ctx.WriteValue("score_distance_bonus_m", m_fDistanceBonus);
		ctx.WriteValue("score_bonus_assistance", m_iBonusAssistance);
		ctx.WriteValue("score_fenetre_assistance_s", m_fFenetreAssistance);
		ctx.WriteValue("score_prime_leader", m_iPrimeLeader);
		ctx.WriteValue("score_fenetre_attribution_s", m_fFenetreAttribution);
		ctx.WriteValue("score_fenetre_deconnexion_s", m_fFenetreDeconnexion);
		ctx.WriteValue("score_reconnexion_min", m_iReconnexionMin);
		ctx.WriteValue("score_kills_min_victoire", m_iKillsMinVictoire);
		ctx.WriteValue("score_serie_soin", m_bSerieSoin);
		ctx.WriteValue("score_serie_munitions", m_bSerieMunitions);
		ctx.WriteValue("score_serie_bots", m_bSerieBots);

		ctx.WriteValue("villes_nombre", m_aVilles.Count());
		for (int i = 0; i < m_aVilles.Count(); i++)
		{
			SPVP_Arena arena = m_aVilles[i];
			string prefix = "ville_" + (i + 1).ToString() + "_";
			ctx.WriteValue(prefix + "nom", arena.m_sNom);
			ctx.WriteValue(prefix + "x", arena.m_fX);
			ctx.WriteValue(prefix + "z", arena.m_fZ);
			ctx.WriteValue(prefix + "rayon", arena.m_fRayonGrand);
			ctx.WriteValue(prefix + "rayon_petit", arena.m_fRayonPetit);
			ctx.WriteValue(prefix + "joueurs_min", arena.m_iJoueursMin);
			ctx.WriteValue(prefix + "joueurs_max", arena.m_iJoueursMax);
			ctx.WriteValue(prefix + "groupe", arena.m_sGroupe);
			ctx.WriteValue(prefix + "actif", arena.m_bActif);
		}

		if (!ctx.SaveToFile(PATH))
			SPVP_Log.Warn("Réglages : impossible d'écrire " + PATH);
	}

	//------------------------------------------------------------------------------------------------
	//! 10 lieux d'Everon (position de leur nom sur la carte), loin du lobby par défaut (base de Levie).
	//! Grande zone 150 m de rayon (environ 300 m de large, V10), petite 100 m. À ajuster dans le fichier ;
	//! lieux_carte.txt (écrit au premier lancement) donne tous les lieux nommés de la carte.
	protected void SetDefaultCities()
	{
		m_aVilles.Clear();
		m_aVilles.Insert(new SPVP_Arena("Morton", 5135, 4012));
		m_aVilles.Insert(new SPVP_Arena("Figari", 5251, 5338));
		m_aVilles.Insert(new SPVP_Arena("Provins", 5486, 6087));
		m_aVilles.Insert(new SPVP_Arena("Montignac", 4773, 7095));
		m_aVilles.Insert(new SPVP_Arena("Chotain", 7086, 6012));
		m_aVilles.Insert(new SPVP_Arena("Entre-Deux", 5767, 7036));
		m_aVilles.Insert(new SPVP_Arena("Régina", 7205, 2324));
		m_aVilles.Insert(new SPVP_Arena("Durras", 8827, 2746));
		m_aVilles.Insert(new SPVP_Arena("Saint-Pierre", 9689, 1558));
		m_aVilles.Insert(new SPVP_Arena("Saint-Philippe", 4503, 10772));
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadInt(JsonLoadContext ctx, string key, inout int value, int min, int max)
	{
		int read;
		if (!ctx.ReadValue(key, read))
			return;
		value = Math.ClampInt(read, min, max);
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadFloat(JsonLoadContext ctx, string key, inout float value, float min, float max)
	{
		float read;
		if (!ctx.ReadValue(key, read))
			return;
		value = Math.Clamp(read, min, max);
	}

	//------------------------------------------------------------------------------------------------
	protected void ReadBool(JsonLoadContext ctx, string key, inout bool value)
	{
		bool read;
		if (ctx.ReadValue(key, read))
			value = read;
	}
}
