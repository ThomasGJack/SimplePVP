//------------------------------------------------------------------------------------------------
// SimplePVP — État de chaque joueur, côté serveur
// Où il est (lobby, ville), où il doit réapparaître, son dernier équipement, ses kills de la manche.
// Livraison 1
//------------------------------------------------------------------------------------------------

enum SPVP_ELieu
{
	AUCUN,
	LOBBY,
	VILLE
}

//------------------------------------------------------------------------------------------------
class SPVP_PlayerInfo
{
	int m_iPlayerId;
	SPVP_ELieu m_eLieu = SPVP_ELieu.AUCUN;		// là où est son personnage vivant
	SPVP_ELieu m_eDestination = SPVP_ELieu.LOBBY;	// là où il apparaîtra la prochaine fois
	bool m_bTransfert;							// personnage supprimé exprès (déploiement, fin de manche) : pas une mort
	bool m_bApparitionPrevue;					// une apparition est déjà programmée
	bool m_bEnFile;								// attend le départ de la prochaine manche (R3)
	bool m_bAttenteChoix;						// mort en ville : attend son choix sur l'écran de mort (D01)
	float m_fMortA;								// heure du monde (ms) de la dernière mort
	int m_iMortJeton;							// numéro de la mort (le retour automatique au lobby ne vise que la bonne)
	string m_sMortTexte;						// D16 : « Tué par X (arme) à 85 m, il lui reste 35 % de vie »
	vector m_vMortCorps;						// position du corps (caméra de mort)
	vector m_vMortDirection;					// direction d'où venait le tueur (caméra, jamais sa position : D18)
	string m_sKitNom;							// armes du dernier équipement, pour l'affichage
	ref array<string> m_aKitsRecents = {};		// D02 : les 3 derniers équipements déployés (le plus récent en tête)
	ref array<string> m_aKitsNoms = {};
	string m_sKit;								// dernier équipement déployé (texte du loadout vanilla)
	int m_iKills;
	int m_iMorts;
	int m_iOrdre;								// ordre d'arrivée à son total de kills (départage)
	int m_iHorsZone;							// secondes passées hors de la zone

	// --- Score de la manche (livraison 4)
	int m_iPoints;								// P01 : 100 par kill + bonus
	int m_iSerie;								// kills depuis la dernière mort (P18)
	int m_iMeilleureSerie;
	int m_iTetes;								// headshots (médaille)
	int m_iPlusLongKill;						// mètres (médaille)
	int m_iAssists;

	// --- Bots (identifiant négatif, jamais confondu avec un joueur)
	bool m_bBot;
	string m_sNom;								// nom affiché d'un bot (« [BOT] Kurt_TR »)
	IEntity m_Bot;								// personnage actuel du bot
	SCR_AIGroup m_Groupe;						// son groupe (un par bot : chacun pour soi)
	float m_fReapparition;						// heure du monde (ms) de sa prochaine apparition, 0 = aucune
	bool m_bPartant;							// ne réapparaîtra pas (trop de bots pour le nombre de joueurs)
	float m_fProchainIndice;					// heure du prochain « secteur vague » (I14)
	vector m_vDernierePosition;					// détecteur de bot coincé
	int m_iImmobile;							// passages sans bouger

	//------------------------------------------------------------------------------------------------
	void SPVP_PlayerInfo(int playerId)
	{
		m_iPlayerId = playerId;
	}

	//------------------------------------------------------------------------------------------------
	//! Nouvelle manche : score remis à zéro
	void ResetManche()
	{
		m_iKills = 0;
		m_iMorts = 0;
		m_iOrdre = 0;
		m_iHorsZone = 0;
		m_iPoints = 0;
		m_iSerie = 0;
		m_iMeilleureSerie = 0;
		m_iTetes = 0;
		m_iPlusLongKill = 0;
		m_iAssists = 0;
	}

	//------------------------------------------------------------------------------------------------
	//! PN05 : score repris d'une ancienne fiche (joueur revenu dans les 5 min)
	void CopyScore(SPVP_PlayerInfo autre)
	{
		m_iKills = autre.m_iKills;
		m_iMorts = autre.m_iMorts;
		m_iOrdre = autre.m_iOrdre;
		m_iPoints = autre.m_iPoints;
		m_iMeilleureSerie = autre.m_iMeilleureSerie;
		m_iTetes = autre.m_iTetes;
		m_iPlusLongKill = autre.m_iPlusLongKill;
		m_iAssists = autre.m_iAssists;
	}
}

//------------------------------------------------------------------------------------------------
class SPVP_Players
{
	protected static ref map<int, ref SPVP_PlayerInfo> s_mInfos;

	//------------------------------------------------------------------------------------------------
	static void Reset()
	{
		s_mInfos = new map<int, ref SPVP_PlayerInfo>();
	}

	//------------------------------------------------------------------------------------------------
	//! Fiche du joueur, créée au besoin
	static SPVP_PlayerInfo Get(int playerId)
	{
		if (!s_mInfos)
			Reset();

		SPVP_PlayerInfo info;
		if (!s_mInfos.Find(playerId, info))
		{
			info = new SPVP_PlayerInfo(playerId);
			s_mInfos.Insert(playerId, info);
		}
		return info;
	}

	//------------------------------------------------------------------------------------------------
	static SPVP_PlayerInfo Find(int playerId)
	{
		if (!s_mInfos)
			return null;

		SPVP_PlayerInfo info;
		s_mInfos.Find(playerId, info);
		return info;
	}

	//------------------------------------------------------------------------------------------------
	static void Remove(int playerId)
	{
		if (s_mInfos)
			s_mInfos.Remove(playerId);
	}

	//------------------------------------------------------------------------------------------------
	static void GetAll(notnull array<SPVP_PlayerInfo> outInfos)
	{
		outInfos.Clear();
		if (!s_mInfos)
			return;

		foreach (int playerId, SPVP_PlayerInfo info : s_mInfos)
		{
			outInfos.Insert(info);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Nom affiché d'un combattant (joueur ou bot)
	static string GetName(SPVP_PlayerInfo info)
	{
		if (info.m_bBot)
			return info.m_sNom;

		string nom = GetGame().GetPlayerManager().GetPlayerName(info.m_iPlayerId);
		nom.Replace("|", "/");
		nom.Replace("\n", " ");
		return nom;
	}

	//------------------------------------------------------------------------------------------------
	//! Personnage vivant d'un combattant (joueur ou bot), ou null
	static IEntity GetAliveEntity(SPVP_PlayerInfo info)
	{
		if (!info)
			return null;

		if (info.m_bBot)
		{
			if (!info.m_Bot || info.m_Bot.IsDeleted() || IsDead(info.m_Bot))
				return null;
			return info.m_Bot;
		}

		return GetAliveCharacter(info.m_iPlayerId);
	}

	//------------------------------------------------------------------------------------------------
	//! Identifiant du combattant qui contrôle cette entité : joueur (> 0), bot (< 0), ou 0 si inconnu
	static int GetFighterId(IEntity entity)
	{
		if (!entity)
			return 0;

		int playerId = GetGame().GetPlayerManager().GetPlayerIdFromControlledEntity(entity);
		if (playerId > 0)
			return playerId;

		if (!s_mInfos)
			return 0;

		foreach (int id, SPVP_PlayerInfo info : s_mInfos)
		{
			if (info.m_bBot && info.m_Bot == entity)
				return id;
		}
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Personnage vivant du joueur, ou null
	static IEntity GetAliveCharacter(int playerId)
	{
		IEntity character = GetGame().GetPlayerManager().GetPlayerControlledEntity(playerId);
		if (!character || IsDead(character))
			return null;
		return character;
	}

	//------------------------------------------------------------------------------------------------
	static bool IsDead(IEntity entity)
	{
		if (!entity)
			return true;

		SCR_ChimeraCharacter character = SCR_ChimeraCharacter.Cast(entity);
		if (!character)
			return false;

		CharacterControllerComponent controller = character.GetCharacterController();
		return controller && controller.GetLifeState() == ECharacterLifeState.DEAD;
	}
}

//------------------------------------------------------------------------------------------------
// SimplePVP — Équipement : lecture et application avec le système de loadout du jeu de base
// (même méthode que les sauvegardes d'arsenal vanilla : prefabs emplacement par emplacement, chargeurs pleins)
//------------------------------------------------------------------------------------------------
class SPVP_Kit
{
	//------------------------------------------------------------------------------------------------
	//! Équipement complet du personnage en texte, ou "" en cas d'échec
	static string Capture(IEntity character)
	{
		if (!character || !character.FindComponent(InventoryStorageManagerComponent))
			return "";

		JsonSaveContext context = new JsonSaveContext();
		if (!SCR_PlayerArsenalLoadout.ReadLoadoutString(character, context))
			return "";

		return context.SaveToString();
	}

	//------------------------------------------------------------------------------------------------
	//! Applique un équipement sur le personnage (serveur)
	static bool Apply(IEntity character, string kit)
	{
		if (!character || kit.IsEmpty())
			return false;

		JsonLoadContext context = new JsonLoadContext();
		if (!context.LoadFromString(kit))
			return false;

		return SCR_PlayerArsenalLoadout.ApplyLoadoutString(character, context);
	}

	//------------------------------------------------------------------------------------------------
	//! Les armes portées, en texte : « M16A2 + M9 » (ou « sans arme »)
	static string Label(IEntity character)
	{
		array<IEntity> armes = {};
		GetWeapons(character, armes);

		string texte = "";
		foreach (IEntity arme : armes)
		{
			BaseWeaponComponent weapon = BaseWeaponComponent.Cast(arme.FindComponent(BaseWeaponComponent));
			if (!weapon || !weapon.GetUIInfo())
				continue;

			if (!texte.IsEmpty())
				texte += " + ";
			texte += WidgetManager.Translate(weapon.GetUIInfo().GetName());
		}

		if (texte.IsEmpty())
			return "sans arme";
		return texte;
	}

	//------------------------------------------------------------------------------------------------
	//! Porte-t-il une arme à feu (fusil, mitrailleuse, pistolet, lance-roquettes…) ?
	static bool HasFirearm(IEntity character)
	{
		array<IEntity> armes = {};
		GetWeapons(character, armes);
		foreach (IEntity arme : armes)
		{
			BaseWeaponComponent weapon = BaseWeaponComponent.Cast(arme.FindComponent(BaseWeaponComponent));
			if (!weapon)
				continue;

			EWeaponType type = weapon.GetWeaponType();
			if (type != EWeaponType.WT_NONE && type != EWeaponType.WT_FRAGGRENADE && type != EWeaponType.WT_SMOKEGRENADE)
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	protected static void GetWeapons(IEntity character, notnull array<IEntity> outWeapons)
	{
		if (!character)
			return;

		BaseWeaponManagerComponent manager = BaseWeaponManagerComponent.Cast(character.FindComponent(BaseWeaponManagerComponent));
		if (manager)
			manager.GetWeaponsList(outWeapons);
	}
}
