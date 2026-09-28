//------------------------------------------------------------------------------------------------
// SimplePVP — Suivi des coups reçus (serveur)
// Pour chaque combattant vivant : qui l'a touché, quand, avec quoi, à la tête ou non.
// Sert à : kill achevé (30 s, P11), assistances (10 s, P15), départ en pleine blessure (10 s, PN06),
// headshot (P13), nom de l'arme dans le fil et l'écran de mort, hit markers (C25).
// Les coups sont notés dans SPVP_Damage (HijackDamageHandling), avant que le jeu applique les dégâts :
// le coup mortel est donc toujours noté avant l'événement de mort.
// Livraison 4
//------------------------------------------------------------------------------------------------

class SPVP_Hit
{
	int m_iTireur;			// combattant qui a touché (joueur > 0, bot < 0)
	float m_fHeure;			// heure du monde (ms)
	string m_sArme;			// clé de traduction du nom d'arme (#AR-…) ou texte
	bool m_bTete;
	bool m_bBalle;			// dégât KINETIC (seul cas où un headshot compte)
}

//------------------------------------------------------------------------------------------------
class SPVP_HitList
{
	IEntity m_Victime;		// le personnage de cette vie : une nouvelle vie repart d'une liste vide
	ref array<ref SPVP_Hit> m_aCoups = {};
}

//------------------------------------------------------------------------------------------------
class SPVP_HitTracker
{
	protected static ref map<int, ref SPVP_HitList> s_mListes;
	protected static ref map<int, float> s_mDernierMarqueur;	// tireur → heure du dernier hit marker envoyé

	protected static const int COUPS_MAX = 12;
	protected static const float MARQUEUR_INTERVALLE_MS = 60;

	//------------------------------------------------------------------------------------------------
	static void Reset()
	{
		s_mListes = new map<int, ref SPVP_HitList>();
		s_mDernierMarqueur = new map<int, float>();
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur, depuis SPVP_Damage : un coup vient d'être reçu (dégâts déjà réduits par la protection)
	static void Record(IEntity victime, notnull BaseDamageContext damageContext)
	{
		if (!victime || !damageContext.instigator || damageContext.damageValue <= 0)
			return;

		EDamageType type = damageContext.damageType;
		if (type == EDamageType.HEALING || type == EDamageType.REGENERATION || type == EDamageType.BLEEDING)
			return;

		// Les tirs sur un corps ne comptent pas
		if (SPVP_Players.IsDead(victime))
			return;

		int victimeId = SPVP_Players.GetFighterId(victime);
		if (victimeId == 0)
			return;

		Instigator instigator = damageContext.instigator;
		IEntity tireurEntite = instigator.GetInstigatorEntity();
		int tireurId = instigator.GetInstigatorPlayerID();
		if (tireurId <= 0)
			tireurId = SPVP_Players.GetFighterId(tireurEntite);
		if (tireurId == 0 || tireurId == victimeId)
			return;

		if (!tireurEntite && tireurId > 0)
			tireurEntite = GetGame().GetPlayerManager().GetPlayerControlledEntity(tireurId);

		if (!s_mListes)
			Reset();

		SPVP_HitList liste;
		if (!s_mListes.Find(victimeId, liste) || liste.m_Victime != victime)
		{
			liste = new SPVP_HitList();
			liste.m_Victime = victime;
			s_mListes.Set(victimeId, liste);
		}

		SPVP_Hit coup = new SPVP_Hit();
		coup.m_iTireur = tireurId;
		coup.m_fHeure = GetGame().GetWorld().GetWorldTime();
		coup.m_sArme = WeaponName(type, damageContext.damageSource, tireurEntite);
		coup.m_bTete = SCR_CharacterHeadHitZone.Cast(damageContext.struckHitZone) != null;
		coup.m_bBalle = type == EDamageType.KINETIC;

		liste.m_aCoups.Insert(coup);
		while (liste.m_aCoups.Count() > COUPS_MAX)
		{
			liste.m_aCoups.RemoveOrdered(0);
		}

		// C25 : hit marker pour un vrai joueur (au plus un toutes les 60 ms : rafales, chevrotine, éclats)
		if (tireurId > 0)
			SendHitMarker(tireurId, coup.m_fHeure);
	}

	//------------------------------------------------------------------------------------------------
	protected static void SendHitMarker(int tireurId, float heure)
	{
		float derniere;
		if (s_mDernierMarqueur.Find(tireurId, derniere) && heure - derniere < MARQUEUR_INTERVALLE_MS)
			return;

		s_mDernierMarqueur.Set(tireurId, heure);
		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerManager().GetPlayerController(tireurId));
		if (pc)
			pc.SPVP_HitMarker(false);
	}

	//------------------------------------------------------------------------------------------------
	//! Nom de ce qui a fait le dégât : arme en main pour une balle, objet pour une grenade, sinon un mot
	static string WeaponName(EDamageType type, IEntity source, IEntity tireur)
	{
		if (type == EDamageType.MELEE)
			return "Corps à corps";

		if (type == EDamageType.FIRE || type == EDamageType.INCENDIARY)
			return "Feu";

		bool explosion = type == EDamageType.EXPLOSIVE || type == EDamageType.FRAGMENTATION || type == EDamageType.PROCESSED_FRAGMENTATION;
		if (explosion && source)
		{
			InventoryItemComponent item = InventoryItemComponent.Cast(source.FindComponent(InventoryItemComponent));
			if (item && item.GetAttributes() && item.GetAttributes().GetUIInfo())
			{
				string nom = item.GetAttributes().GetUIInfo().GetName();
				if (!nom.IsEmpty())
					return nom;
			}
		}

		string enMain = CurrentWeaponName(tireur);
		if (!enMain.IsEmpty())
			return enMain;

		if (explosion)
			return "Explosion";
		return "";
	}

	//------------------------------------------------------------------------------------------------
	static string CurrentWeaponName(IEntity tireur)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(tireur);
		if (!character || !character.GetCharacterController())
			return "";

		BaseWeaponManagerComponent armes = character.GetCharacterController().GetWeaponManagerComponent();
		if (!armes)
			return "";

		BaseWeaponComponent arme = armes.GetCurrentWeapon();
		if (!arme || !arme.GetUIInfo())
			return "";

		return arme.GetUIInfo().GetName();
	}

	//------------------------------------------------------------------------------------------------
	protected static SPVP_HitList GetList(int victimeId, IEntity victime)
	{
		if (!s_mListes)
			return null;

		SPVP_HitList liste;
		if (!s_mListes.Find(victimeId, liste))
			return null;

		// Liste d'une vie précédente
		if (victime && liste.m_Victime != victime)
			return null;
		return liste;
	}

	//------------------------------------------------------------------------------------------------
	//! Dernier coup d'un tireur précis dans la fenêtre (secondes), ou null
	static SPVP_Hit LastHitFrom(int victimeId, IEntity victime, int tireurId, float fenetreS)
	{
		SPVP_HitList liste = GetList(victimeId, victime);
		if (!liste)
			return null;

		float limite = GetGame().GetWorld().GetWorldTime() - fenetreS * 1000;
		for (int i = liste.m_aCoups.Count() - 1; i >= 0; i--)
		{
			SPVP_Hit coup = liste.m_aCoups[i];
			if (coup.m_fHeure < limite)
				return null;
			if (coup.m_iTireur == tireurId)
				return coup;
		}
		return null;
	}

	//------------------------------------------------------------------------------------------------
	//! Dernier tireur (n'importe lequel) dans la fenêtre, ou null
	static SPVP_Hit LastHit(int victimeId, IEntity victime, float fenetreS)
	{
		SPVP_HitList liste = GetList(victimeId, victime);
		if (!liste || liste.m_aCoups.IsEmpty())
			return null;

		SPVP_Hit coup = liste.m_aCoups[liste.m_aCoups.Count() - 1];
		if (coup.m_fHeure < GetGame().GetWorld().GetWorldTime() - fenetreS * 1000)
			return null;
		return coup;
	}

	//------------------------------------------------------------------------------------------------
	//! Tous les tireurs dans la fenêtre, sans doublon (assistances)
	static void AttackersSince(int victimeId, IEntity victime, float fenetreS, notnull array<int> outTireurs)
	{
		outTireurs.Clear();
		SPVP_HitList liste = GetList(victimeId, victime);
		if (!liste)
			return;

		float limite = GetGame().GetWorld().GetWorldTime() - fenetreS * 1000;
		foreach (SPVP_Hit coup : liste.m_aCoups)
		{
			if (coup.m_fHeure >= limite && outTireurs.Find(coup.m_iTireur) < 0)
				outTireurs.Insert(coup.m_iTireur);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Vie terminée : la liste est oubliée (après le calcul du kill)
	static void Forget(int victimeId)
	{
		if (s_mListes)
			s_mListes.Remove(victimeId);
	}
}
