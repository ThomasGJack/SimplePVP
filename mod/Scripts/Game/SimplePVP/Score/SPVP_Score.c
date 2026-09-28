//------------------------------------------------------------------------------------------------
// SimplePVP — Qui marque, combien, et ce qu'on annonce (serveur)
// - P10, P11 : kill direct ; mort « bête » (chute, grenade, zone…) créditée au dernier tireur des 30 s ;
//   sinon simple mort. Saignement : toujours au tireur (c'est sa blessure qui tue).
// - P12 à P17 : 100 par kill (un bot vaut un joueur), +50 tête, +50 au-delà de 150 m, +50 par assistance
//   (10 s), +100 de prime sur le 1er. Pas de pénalité de mort.
// - P18, P19 : à 3, 5 et 10 kills sans mourir, soin complet et chargeurs pleins.
// - PN06 : un joueur blessé qui quitte le serveur compte comme mort, le kill va au dernier tireur (10 s).
// Le serveur n'envoie que des noms et des nombres ; chaque écran compose ses phrases.
// Livraison 4
//------------------------------------------------------------------------------------------------

enum SPVP_EKillCause
{
	DIRECT,
	SAIGNEMENT,		// le tireur ne l'a plus touché depuis longtemps, mais sa blessure a tué
	ACHEVE,			// mort seule (chute, grenade, zone…) créditée au dernier tireur
	SEUL,			// mort sans tireur
	DECONNEXION		// a quitté le serveur en pleine blessure
}

//! Lignes du popup « +100 Élimination » (P24)
enum SPVP_ELigne
{
	KILL,
	TETE,
	DISTANCE,
	DISTANCE_M,		// distance en mètres, affichée avec la ligne DISTANCE (pas une ligne à part)
	PRIME,
	ASSIST,
	SERIE,
	ACHEVE,
	FUI
}

//! Bandeaux pour tous (P25)
enum SPVP_EBandeau
{
	SERIE,
	LEADER
}

//! Marques du fil des éliminations
class SPVP_KillFlags
{
	static const int TETE = 1;
	static const int ACHEVE = 2;
	static const int FUI = 4;
	static const int SEUL = 8;
}

//------------------------------------------------------------------------------------------------
class SPVP_Score
{
	static ref array<int> PALIERS_SERIE = {3, 5, 10};
	static ref array<int> ANNONCES_SERIE = {3, 5, 10, 15, 20};

	//------------------------------------------------------------------------------------------------
	//! Serveur : un combattant est mort en ville pendant la manche. Renvoie le combattant crédité (0 = aucun).
	static int OnDeath(notnull SPVP_RoundManagerComponent rounds, notnull SPVP_PlayerInfo victim, IEntity corps, int instigateurId, IEntity tueurEntite)
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		int victimId = victim.m_iPlayerId;

		victim.m_iMorts++;
		victim.m_iSerie = 0;

		// Qui marque ?
		int creditId = 0;
		int cause = SPVP_EKillCause.SEUL;
		SPVP_Hit coup;

		if (instigateurId != 0 && instigateurId != victimId && SPVP_Players.Find(instigateurId))
		{
			creditId = instigateurId;
			coup = SPVP_HitTracker.LastHitFrom(victimId, corps, creditId, settings.m_fFenetreAttribution);
			if (coup)
				cause = SPVP_EKillCause.DIRECT;
			else
				cause = SPVP_EKillCause.SAIGNEMENT;
		}
		else
		{
			coup = SPVP_HitTracker.LastHit(victimId, corps, settings.m_fFenetreAttribution);
			if (coup && coup.m_iTireur != victimId && SPVP_Players.Find(coup.m_iTireur))
			{
				creditId = coup.m_iTireur;
				cause = SPVP_EKillCause.ACHEVE;
			}
		}

		SPVP_PlayerInfo tueur = SPVP_Players.Find(creditId);
		if (!tueur)
		{
			NoteDeathText(victim, corps, null, null, cause, null, 0);
			rounds.SendKillFeed("", "", SPVP_Players.GetName(victim), SPVP_KillFlags.SEUL, 0);
			SPVP_HitTracker.Forget(victimId);
			return 0;
		}

		if (!tueurEntite || cause != SPVP_EKillCause.DIRECT)
		{
			IEntity vivant = SPVP_Players.GetAliveEntity(tueur);
			if (vivant)
				tueurEntite = vivant;
		}

		Credit(rounds, tueur, victim, corps, tueurEntite, cause, coup, settings.m_fFenetreAssistance);
		SPVP_HitTracker.Forget(victimId);
		return creditId;
	}

	//------------------------------------------------------------------------------------------------
	//! PN06 : départ en pleine blessure. Renvoie le combattant crédité (0 = aucun), après avoir compté la mort.
	static int OnWoundedLeave(notnull SPVP_RoundManagerComponent rounds, notnull SPVP_PlayerInfo victim, IEntity corps)
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		SPVP_Hit coup = SPVP_HitTracker.LastHit(victim.m_iPlayerId, corps, settings.m_fFenetreDeconnexion);
		if (!coup)
			return 0;

		victim.m_iMorts++;
		victim.m_iSerie = 0;

		SPVP_PlayerInfo tueur = SPVP_Players.Find(coup.m_iTireur);
		if (!tueur || tueur == victim)
			return 0;

		Credit(rounds, tueur, victim, corps, SPVP_Players.GetAliveEntity(tueur), SPVP_EKillCause.DECONNEXION, coup, settings.m_fFenetreAssistance);
		SPVP_HitTracker.Forget(victim.m_iPlayerId);
		return tueur.m_iPlayerId;
	}

	//------------------------------------------------------------------------------------------------
	protected static void Credit(SPVP_RoundManagerComponent rounds, SPVP_PlayerInfo tueur, SPVP_PlayerInfo victim, IEntity corps, IEntity tueurEntite, int cause, SPVP_Hit coup, float fenetreAssistance)
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		int leaderAvant = rounds.GetLeaderId();

		array<int> types = {};
		array<int> valeurs = {};

		// KILL (toutes causes)
		int points = settings.m_iPointsKill;
		types.Insert(SPVP_ELigne.KILL);
		valeurs.Insert(settings.m_iPointsKill);

		if (cause == SPVP_EKillCause.ACHEVE)
		{
			types.Insert(SPVP_ELigne.ACHEVE);
			valeurs.Insert(0);
		}
		else if (cause == SPVP_EKillCause.DECONNEXION)
		{
			types.Insert(SPVP_ELigne.FUI);
			valeurs.Insert(0);
		}

		// Distance entre les deux personnages au moment de la mort
		int distance = 0;
		if (corps && tueurEntite)
			distance = Math.Round(vector.Distance(corps.GetOrigin(), tueurEntite.GetOrigin()));

		// TÊTE (P13) : kill direct, balle, coup mortel à la tête dans la dernière seconde
		bool tete = false;
		if (cause == SPVP_EKillCause.DIRECT && coup && coup.m_bTete && coup.m_bBalle)
		{
			if (GetGame().GetWorld().GetWorldTime() - coup.m_fHeure <= 1000)
				tete = true;
		}
		if (tete)
		{
			points += settings.m_iBonusTete;
			types.Insert(SPVP_ELigne.TETE);
			valeurs.Insert(settings.m_iBonusTete);
			tueur.m_iTetes++;
		}

		// DISTANCE (P14)
		if (cause == SPVP_EKillCause.DIRECT && distance >= settings.m_fDistanceBonus)
		{
			points += settings.m_iBonusDistance;
			types.Insert(SPVP_ELigne.DISTANCE);
			valeurs.Insert(settings.m_iBonusDistance);
			types.Insert(SPVP_ELigne.DISTANCE_M);
			valeurs.Insert(distance);
		}
		if (cause == SPVP_EKillCause.DIRECT && distance > tueur.m_iPlusLongKill)
			tueur.m_iPlusLongKill = distance;

		// PRIME (P17) : la victime était le 1er
		if (leaderAvant != 0 && leaderAvant == victim.m_iPlayerId && victim.m_iKills > 0)
		{
			points += settings.m_iPrimeLeader;
			types.Insert(SPVP_ELigne.PRIME);
			valeurs.Insert(settings.m_iPrimeLeader);
		}

		tueur.m_iKills++;
		tueur.m_iPoints += points;
		tueur.m_iOrdre = rounds.NextOrdre();

		// SÉRIE (P18) : seulement si le tueur est encore en vie
		IEntity vivant = SPVP_Players.GetAliveEntity(tueur);
		if (vivant)
		{
			tueur.m_iSerie++;
			if (tueur.m_iSerie > tueur.m_iMeilleureSerie)
				tueur.m_iMeilleureSerie = tueur.m_iSerie;

			if (PALIERS_SERIE.Find(tueur.m_iSerie) >= 0 && (!tueur.m_bBot || settings.m_bSerieBots))
			{
				StreakReward(vivant, settings.m_bSerieSoin, settings.m_bSerieMunitions);
				types.Insert(SPVP_ELigne.SERIE);
				valeurs.Insert(tueur.m_iSerie);
			}

			if (!tueur.m_bBot && ANNONCES_SERIE.Find(tueur.m_iSerie) >= 0)
				rounds.SendBanner(SPVP_EBandeau.SERIE, SPVP_Players.GetName(tueur), tueur.m_iSerie);
		}

		// Arme
		string arme = "";
		if (coup)
			arme = coup.m_sArme;
		if (arme.IsEmpty())
			arme = SPVP_HitTracker.CurrentWeaponName(tueurEntite);

		// Fil des éliminations pour tous
		int flags = 0;
		if (tete)
			flags |= SPVP_KillFlags.TETE;
		if (cause == SPVP_EKillCause.ACHEVE)
			flags |= SPVP_KillFlags.ACHEVE;
		if (cause == SPVP_EKillCause.DECONNEXION)
			flags |= SPVP_KillFlags.FUI;
		rounds.SendKillFeed(SPVP_Players.GetName(tueur), arme, SPVP_Players.GetName(victim), flags, distance);

		// Popup et croix rouge pour le tueur
		if (!tueur.m_bBot)
		{
			SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(tueur.m_iPlayerId));
			if (pc)
				pc.SPVP_KillConfirm(types, valeurs, SPVP_Players.GetName(victim), cause == SPVP_EKillCause.DIRECT);
		}

		// Assistances (P15) : tous ceux qui ont touché dans les 10 s, sauf le tueur
		array<int> assistants = {};
		SPVP_HitTracker.AttackersSince(victim.m_iPlayerId, corps, fenetreAssistance, assistants);
		foreach (int assistantId : assistants)
		{
			if (assistantId == tueur.m_iPlayerId || assistantId == victim.m_iPlayerId)
				continue;

			SPVP_PlayerInfo assistant = SPVP_Players.Find(assistantId);
			if (!assistant)
				continue;

			assistant.m_iPoints += settings.m_iBonusAssistance;
			assistant.m_iAssists++;
			if (!assistant.m_bBot)
			{
				SCR_PlayerController pcAssist = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(assistantId));
				if (pcAssist)
				{
					array<int> typesAssist = {};
					array<int> valeursAssist = {};
					typesAssist.Insert(SPVP_ELigne.ASSIST);
					valeursAssist.Insert(settings.m_iBonusAssistance);
					pcAssist.SPVP_KillConfirm(typesAssist, valeursAssist, SPVP_Players.GetName(victim), false);
				}
			}
		}

		NoteDeathText(victim, corps, tueur, tueurEntite, cause, coup, distance);
		SPVP_Log.Info(string.Format("%1 a tué %2 (%3, %4 m, cause %5, +%6 pts)", SPVP_Players.GetName(tueur), SPVP_Players.GetName(victim), WidgetManager.Translate(arme), distance, cause, points));
	}

	//------------------------------------------------------------------------------------------------
	//! D16, D17 : ce que le joueur mort verra (texte, position du corps, direction du tueur pour la caméra)
	protected static void NoteDeathText(SPVP_PlayerInfo victim, IEntity corps, SPVP_PlayerInfo tueur, IEntity tueurEntite, int cause, SPVP_Hit coup, int distance)
	{
		if (victim.m_bBot)
			return;

		victim.m_vMortDirection = vector.Zero;
		if (corps)
			victim.m_vMortCorps = corps.GetOrigin();

		if (!tueur)
		{
			victim.m_sMortTexte = "Tu es mort (chute, explosion ou hors zone)";
			return;
		}

		if (corps && tueurEntite)
		{
			vector direction = tueurEntite.GetOrigin() - corps.GetOrigin();
			direction[1] = 0;
			if (direction.Length() > 0.1)
				victim.m_vMortDirection = direction.Normalized();
		}

		string arme = "arme inconnue";
		if (coup && !coup.m_sArme.IsEmpty())
			arme = WidgetManager.Translate(coup.m_sArme);
		else if (tueurEntite)
		{
			string enMain = SPVP_HitTracker.CurrentWeaponName(tueurEntite);
			if (!enMain.IsEmpty())
				arme = WidgetManager.Translate(enMain);
		}

		int vie = 0;
		IEntity vivant = SPVP_Players.GetAliveEntity(tueur);
		if (vivant)
		{
			SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(vivant.FindComponent(SCR_CharacterDamageManagerComponent));
			if (damage)
				vie = Math.Round(damage.GetHealthScaled() * 100);
		}

		int metres = Math.Round(distance / 5.0) * 5;
		string nom = SPVP_Players.GetName(tueur);
		if (cause == SPVP_EKillCause.ACHEVE)
		{
			victim.m_sMortTexte = string.Format("Achevé : %1 t'avait touché juste avant (%2).", nom, arme);
			return;
		}

		string tete = "";
		if (coup && coup.m_bTete && cause == SPVP_EKillCause.DIRECT)
			tete = ", dans la tête";

		if (vie > 0)
			victim.m_sMortTexte = string.Format("Tué par %1 (%2) à %3 m%4. Il lui reste %5 pour cent de vie.", nom, arme, metres, tete, vie);
		else
			victim.m_sMortTexte = string.Format("Tué par %1 (%2) à %3 m%4. Il est mort lui aussi.", nom, arme, metres, tete);
	}

	//------------------------------------------------------------------------------------------------
	//! P18 : soin complet (saignements arrêtés) puis chargeurs pleins, y compris celui engagé dans l'arme
	static void StreakReward(IEntity character, bool soin, bool munitions)
	{
		if (!character)
			return;

		if (soin)
		{
			SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(character.FindComponent(SCR_CharacterDamageManagerComponent));
			if (damage)
				damage.FullHeal();
		}

		if (!munitions)
			return;

		ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
		if (!chimera || !chimera.GetCharacterController())
			return;

		InventoryStorageManagerComponent inventaire = chimera.GetCharacterController().GetInventoryStorageManager();
		if (!inventaire)
			return;

		array<IEntity> objets = {};
		inventaire.GetItems(objets);
		foreach (IEntity objet : objets)
		{
			if (!objet)
				continue;

			BaseMagazineComponent chargeur = BaseMagazineComponent.Cast(objet.FindComponent(BaseMagazineComponent));
			if (chargeur)
				chargeur.SetAmmoCount(chargeur.GetMaxAmmoCount());

			BaseWeaponComponent arme = BaseWeaponComponent.Cast(objet.FindComponent(BaseWeaponComponent));
			if (!arme)
				continue;

			array<BaseMuzzleComponent> bouches = {};
			arme.GetMuzzlesList(bouches);
			foreach (BaseMuzzleComponent bouche : bouches)
			{
				if (!bouche)
					continue;
				BaseMagazineComponent engage = bouche.GetMagazine();
				if (engage)
					engage.SetAmmoCount(engage.GetMaxAmmoCount());
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! P20 : médailles de la manche, vrais joueurs seulement. Une ligne « code|nom|valeur » par médaille.
	static string Medals(notnull array<SPVP_PlayerInfo> joueurs)
	{
		SPVP_PlayerInfo tir, tetes, serie, morts;
		foreach (SPVP_PlayerInfo info : joueurs)
		{
			if (info.m_bBot)
				continue;

			if (info.m_iPlusLongKill > 0 && (!tir || info.m_iPlusLongKill > tir.m_iPlusLongKill))
				tir = info;
			if (info.m_iTetes > 0 && (!tetes || info.m_iTetes > tetes.m_iTetes))
				tetes = info;
			if (info.m_iMeilleureSerie > 1 && (!serie || info.m_iMeilleureSerie > serie.m_iMeilleureSerie))
				serie = info;
			if (info.m_iMorts > 0 && (!morts || info.m_iMorts > morts.m_iMorts))
				morts = info;
		}

		string texte = "";
		if (tir)
			texte += string.Format("tir|%1|%2\n", SPVP_Players.GetName(tir), tir.m_iPlusLongKill);
		if (tetes)
			texte += string.Format("tetes|%1|%2\n", SPVP_Players.GetName(tetes), tetes.m_iTetes);
		if (serie)
			texte += string.Format("serie|%1|%2\n", SPVP_Players.GetName(serie), serie.m_iMeilleureSerie);
		if (morts)
			texte += string.Format("morts|%1|%2\n", SPVP_Players.GetName(morts), morts.m_iMorts);
		return texte;
	}
}
