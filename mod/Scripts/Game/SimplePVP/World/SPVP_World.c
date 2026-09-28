//------------------------------------------------------------------------------------------------
// SimplePVP — Le monde autour des manches (serveur)
// - Météo et heure au hasard, figées pendant la manche ; environ 1 manche sur 6 de nuit de pleine lune (V21-V23)
// - Toutes les portes de la ville ouvertes au début de chaque manche (C19)
// - Corps : 30 au plus, 2 min au sol ; objets tombés : 2 min en ville, 1 min au lobby ; ville propre à chaque vote (AN03, L23)
// - Mines et charges retirées à la mort de celui qui les a posées, et à chaque vote (C21)
// - lieux_carte.txt : tous les lieux nommés d'Everon avec leurs coordonnées, pour choisir les villes
// - Verrou : un serveur dédié sans la bonne clé ne lance aucune manche (S23)
// Livraison 2
//------------------------------------------------------------------------------------------------

class SPVP_World
{
	protected static ref array<IEntity> s_aCorps;
	protected static ref array<IEntity> s_aPieges;			// mines et charges posées
	protected static ref array<int> s_aPoseurs;				// qui les a posées (même index)
	protected static ref array<BaseDoorComponent> s_aPortes;
	protected static bool s_bNuitPrecedente;
	protected static bool s_bDateFixee;
	protected static bool s_bAbonne;

	protected static const int PORTES_PAR_PAQUET = 40;
	protected static const string LIEUX = "$profile:SimplePVP/lieux_carte.txt";

	//------------------------------------------------------------------------------------------------
	//! Nouveau lancement du monde (les statiques survivent entre deux essais dans Workbench)
	static void Reset()
	{
		s_aCorps = {};
		s_aPieges = {};
		s_aPoseurs = {};
		s_aPortes = {};
		s_bNuitPrecedente = false;
		s_bDateFixee = false;
		if (s_bAbonne)
			SCR_PlaceableInventoryItemComponent.GetOnPlacementDoneInvoker().Remove(OnPlaced);
		s_bAbonne = false;
	}

	//------------------------------------------------------------------------------------------------
	//! Démarrage du serveur : suivi des objets posés, liste des lieux de la carte
	static void Init()
	{
		if (!s_bAbonne)
		{
			SCR_PlaceableInventoryItemComponent.GetOnPlacementDoneInvoker().Insert(OnPlaced);
			s_bAbonne = true;
		}
		WritePlaces();
	}

	//================================================================================================
	// Météo et heure
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	//! Tire la météo et l'heure de la manche ; renvoie vrai si c'est une manche de nuit
	static bool PrepareWeather()
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		ChimeraWorld world = GetGame().GetWorld();
		if (!world)
			return false;

		TimeAndWeatherManagerEntity manager = world.GetTimeAndWeatherManager();
		if (!manager)
		{
			SPVP_Log.Warn("Monde : pas de gestionnaire d'heure et de météo");
			return false;
		}

		// Date de pleine lune, une fois pour toutes (18 juillet 1989)
		if (!s_bDateFixee)
		{
			manager.SetDate(1989, 7, 18);
			s_bDateFixee = true;
		}

		bool nuit = settings.m_iNuitChance > 0 && !s_bNuitPrecedente && Math.RandomInt(0, settings.m_iNuitChance) == 0;
		s_bNuitPrecedente = nuit;

		float heure;
		array<string> meteos = {};
		if (nuit)
		{
			// Nuit claire : minuit à peu près, jamais de ciel couvert qui cacherait la lune
			heure = Math.RandomFloat(23.5, 24.5);
			if (heure >= 24)
				heure -= 24;
			meteos.Insert("Clear");
			meteos.Insert("Cloudy");
		}
		else
		{
			float lever = 6;
			float coucher = 20;
			if (manager.GetSunriseHour(lever))
				manager.GetSunsetHour(coucher);
			heure = Math.RandomFloat(lever + 1, coucher - 1);
			meteos.Insert("Clear");
			meteos.Insert("Clear");
			meteos.Insert("Cloudy");
			meteos.Insert("Overcast");
		}

		manager.ForceWeatherTo(true, meteos.GetRandomElement(), 0.0);
		manager.SetFogAmountOverride(true, 0);
		manager.SetIsDayAutoAdvanced(false);
		manager.SetTimeOfTheDay(heure, true);

		// Éclaircissement de nuit du jeu (transmis aux joueurs par le composant lui-même)
		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
		{
			SCR_NightModeGameModeComponent nightMode = SCR_NightModeGameModeComponent.Cast(gameMode.FindComponent(SCR_NightModeGameModeComponent));
			if (nightMode)
				nightMode.EnableGlobalNightMode(nuit && settings.m_bEclaircirNuit);
		}

		return nuit;
	}

	//================================================================================================
	// Portes
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	//! C19 : toutes les portes du cercle ouvertes, par paquets pour ne pas faire ramer le serveur
	static void OpenDoors(vector center, float rayon)
	{
		if (!SPVP_Settings.Get().m_bOuvrirPortes)
			return;

		s_aPortes.Clear();
		GetGame().GetWorld().QueryEntitiesBySphere(center, rayon + 20, AddDoor, null, EQueryEntitiesFlags.ALL);
		SPVP_Log.Info(string.Format("Monde : %1 porte(s) à ouvrir", s_aPortes.Count()));
		OpenDoorBatch();
	}

	//------------------------------------------------------------------------------------------------
	protected static bool AddDoor(IEntity entity)
	{
		BaseDoorComponent door = BaseDoorComponent.Cast(entity.FindComponent(BaseDoorComponent));
		if (door)
			s_aPortes.Insert(door);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static void OpenDoorBatch()
	{
		RplId instigator = RplId.Invalid();
		int count = 0;
		while (!s_aPortes.IsEmpty() && count < PORTES_PAR_PAQUET)
		{
			BaseDoorComponent door = s_aPortes[s_aPortes.Count() - 1];
			s_aPortes.Remove(s_aPortes.Count() - 1);
			if (door)
				door.SetControlValue(1, instigator);
			count++;
		}

		if (!s_aPortes.IsEmpty())
			GetGame().GetCallqueue().CallLater(OpenDoorBatch, 100, false);
	}

	//================================================================================================
	// Corps, objets, mines
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	//! Un combattant vient de mourir : son corps entre dans la file (30 au plus), ses mines disparaissent
	static void OnFighterDied(IEntity corps, int fighterId)
	{
		if (!corps)
			return;

		s_aCorps.Insert(corps);
		int max = SPVP_Settings.Get().m_iCorpsMax;
		while (s_aCorps.Count() > max)
		{
			DeleteEntity(s_aCorps[0]);
			s_aCorps.RemoveOrdered(0);
		}

		if (fighterId != 0)
			RemoveTraps(fighterId);
	}

	//------------------------------------------------------------------------------------------------
	//! Grand nettoyage avant la manche suivante (pendant le vote)
	static void CleanAll()
	{
		foreach (IEntity corps : s_aCorps)
		{
			DeleteEntity(corps);
		}
		s_aCorps.Clear();

		foreach (IEntity piege : s_aPieges)
		{
			DeleteEntity(piege);
		}
		s_aPieges.Clear();
		s_aPoseurs.Clear();

		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
		{
			SCR_GarbageSystem garbage = SCR_GarbageSystem.GetByEntityWorld(gameMode);
			if (garbage)
				garbage.Flush(0);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void OnPlaced(ChimeraCharacter user, SCR_PlaceableInventoryItemComponent item)
	{
		if (!Replication.IsServer() || !user || !item)
			return;

		IEntity objet = item.GetOwner();
		if (!objet)
			return;

		if (!objet.FindComponent(SCR_MineInventoryItemComponent) && !objet.FindComponent(SCR_ExplosiveChargeInventoryItemComponent))
			return;

		int poseur = SPVP_Players.GetFighterId(user);
		if (poseur == 0)
			return;

		int index = s_aPieges.Find(objet);
		if (index >= 0)
		{
			s_aPoseurs[index] = poseur;		// ramassé puis reposé par quelqu'un d'autre
			return;
		}

		s_aPieges.Insert(objet);
		s_aPoseurs.Insert(poseur);
	}

	//------------------------------------------------------------------------------------------------
	//! C21 : les pièges d'un combattant disparaissent à sa mort ou à son départ
	static void RemoveTraps(int fighterId)
	{
		for (int i = s_aPieges.Count() - 1; i >= 0; i--)
		{
			if (s_aPoseurs[i] != fighterId)
				continue;

			DeleteEntity(s_aPieges[i]);
			s_aPieges.Remove(i);
			s_aPoseurs.Remove(i);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void DeleteEntity(IEntity entity)
	{
		if (!entity || entity.IsDeleted())
			return;

		SCR_BaseGameMode gameMode = SCR_BaseGameMode.Cast(GetGame().GetGameMode());
		if (gameMode)
		{
			SCR_GarbageSystem garbage = SCR_GarbageSystem.GetByEntityWorld(gameMode);
			if (garbage)
				garbage.Withdraw(entity);
		}
		SCR_EntityHelper.DeleteEntityAndChildren(entity);
	}

	//================================================================================================
	// Lieux de la carte et verrou
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	//! lieux_carte.txt : écrit une fois (s'il manque), pour aider à choisir les villes et le lobby
	protected static void WritePlaces()
	{
		if (FileIO.FileExists(LIEUX))
			return;

		SCR_MapEntity mapEntity = SCR_MapEntity.GetMapInstance();
		if (!mapEntity)
			return;

		FileHandle file = FileIO.OpenFile(LIEUX, FileMode.WRITE);
		if (!file)
			return;

		file.WriteLine("Lieux nommés de la carte (type ; nom ; x ; z). Pour une ville de la rotation, recopier x et z dans reglages.json.");
		WritePlacesOfType(file, mapEntity, EMapDescriptorType.MDT_NAME_CITY, "ville");
		WritePlacesOfType(file, mapEntity, EMapDescriptorType.MDT_NAME_TOWN, "bourg");
		WritePlacesOfType(file, mapEntity, EMapDescriptorType.MDT_NAME_VILLAGE, "village");
		WritePlacesOfType(file, mapEntity, EMapDescriptorType.MDT_NAME_SETTLEMENT, "hameau");
		WritePlacesOfType(file, mapEntity, EMapDescriptorType.MDT_NAME_LOCAL, "lieu-dit");
		WritePlacesOfType(file, mapEntity, EMapDescriptorType.MDT_BASE, "base");
		WritePlacesOfType(file, mapEntity, EMapDescriptorType.MDT_PORT, "port");
		WritePlacesOfType(file, mapEntity, EMapDescriptorType.MDT_AIRPORT, "aéroport");
		file.Close();
		SPVP_Log.Info("Lieux de la carte écrits dans " + LIEUX);
	}

	//------------------------------------------------------------------------------------------------
	protected static void WritePlacesOfType(FileHandle file, SCR_MapEntity mapEntity, int type, string label)
	{
		array<MapItem> items = {};
		mapEntity.GetByType(items, type);
		foreach (MapItem item : items)
		{
			if (!item)
				continue;
			IEntity location = item.Entity();
			if (!location)
				continue;

			vector position = location.GetOrigin();
			string nom = WidgetManager.Translate(item.GetDisplayName());
			file.WriteLine(string.Format("%1 ; %2 ; %3 ; %4", label, nom, Math.Round(position[0]), Math.Round(position[2])));
		}
	}

	//------------------------------------------------------------------------------------------------
	//! S23 : un serveur dédié ne lance de manche qu'avec la clé du serveur officiel (la clé n'est pas dans le mod,
	//! seulement deux empreintes). Workbench et parties hébergées depuis le jeu : pas de contrôle.
	static bool IsAuthorized()
	{
		#ifdef WORKBENCH
		return true;
		#else
		if (!System.IsConsoleApp())
			return true;

		string cle = SPVP_Settings.Get().m_sCleServeur;
		return Empreinte(cle, 31, 50000017) == 44265681 && Empreinte(cle, 37, 49999991) == 8154028;
		#endif
	}

	//------------------------------------------------------------------------------------------------
	protected static int Empreinte(string texte, int multiplicateur, int modulo)
	{
		int valeur = 7;
		for (int i = 0; i < texte.Length(); i++)
		{
			valeur = (valeur * multiplicateur + texte[i].ToAscii()) % modulo;
		}
		return valeur;
	}
}

//------------------------------------------------------------------------------------------------
//! AN03, L23 : durée de vie au sol (corps et objets 2 min en ville, objets 1 min au lobby)
modded class SCR_GarbageSystem
{
	//------------------------------------------------------------------------------------------------
	override protected float OnInsertRequested(IEntity entity, float lifetime)
	{
		lifetime = super.OnInsertRequested(entity, lifetime);
		if (!entity || lifetime <= 0)
			return lifetime;

		if (!ChimeraCharacter.Cast(entity) && SPVP_Lobby.IsInLobby(entity.GetOrigin()))
			return 60;

		return 120;
	}
}
