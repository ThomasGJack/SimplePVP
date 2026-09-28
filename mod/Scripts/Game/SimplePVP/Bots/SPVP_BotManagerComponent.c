//------------------------------------------------------------------------------------------------
// SimplePVP — Bots (IA CRX Enfusion A.I.), côté serveur. À poser sur le mode de jeu.
// - Nombre par paliers selon les joueurs connectés, lobby compris (I03 à I06) : 1-3 → 10, 4-7 → 6, 8-11 → 3, 12+ → 0.
// - Pas de bot tant qu'aucun vrai joueur ne s'est déployé dans la manche (I08) ; tous retirés si le serveur se vide,
//   et à la fin de chaque manche.
// - Un bot en trop finit sa vie et ne réapparaît pas (I07).
// - Réapparition : même délai et mêmes points que les joueurs (I10, I11).
// - Chacun pour soi : chaque bot a son propre groupe, faction SPVP hostile à elle-même (I02).
// - Soldat tiré au hasard parmi les soldats US, soviétiques et FIA du jeu (arme et chargeurs assortis, lance-roquettes
//   possible) (I20, I21). Nom réaliste avec [BOT] (I24).
// - Quand un bot n'a rien à faire : il va voir un secteur vague d'environ 100 m où se trouve quelqu'un (I14) ;
//   s'il sort de la zone ou reste bloqué, il reçoit un nouvel ordre. Le reste (tirs entendus, fouille des maisons,
//   combat) est fait par CRX.
// Livraison « bots » (avancée avant les livraisons 2 à 4 à la demande de Jack)
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "SimplePVP", description: "Bots CRX de SimplePVP")]
class SPVP_BotManagerComponentClass : SCR_BaseGameModeComponentClass
{
}

class SPVP_BotManagerComponent : SCR_BaseGameModeComponent
{
	[Attribute("{000CD338713F2B5A}Prefabs/AI/Groups/Group_Base.et", UIWidgets.ResourceNamePicker, "Groupe IA vide (un par bot)", "et", category: "SimplePVP")]
	protected ResourceName m_sGroupPrefab;

	[Attribute("", UIWidgets.ResourceNamePicker, "Soldats tirés au hasard pour les bots (tenue, arme et chargeurs)", "et", category: "SimplePVP")]
	protected ref array<ResourceName> m_aSoldats;

	[Attribute("SPVP", UIWidgets.EditBox, "Clé de la faction des bots", category: "SimplePVP")]
	protected FactionKey m_sFactionKey;

	protected static SPVP_BotManagerComponent s_Instance;

	protected ref array<int> m_aSlots = {};				// identifiants (négatifs) des bots en service
	protected int m_iProchainId = -1;
	protected ref array<string> m_aNomsPris = {};
	protected int m_iMancheVue = -1;
	protected bool m_bDeploiementVu;
	protected float m_fProchaineCreation;

	protected static const float BORD_ZONE = 10;		// un bot à moins de 10 m du bord est renvoyé vers l'intérieur
	protected static const int IMMOBILE_MAX = 45;		// secondes sans bouger ni combattre avant un nouvel ordre

	protected static ref array<string> s_aNoms = {
		"Kurt_TR", "Mehdi_92", "Lukas.K", "Wei_Long", "Ivan_Petrov", "Jean-Luc", "Emre_34", "Tomasz_W", "Kenji", "Diego.M",
		"Olek", "Hugo_B", "Can_Yilmaz", "Zhang_Wei", "Sasha", "Pierre_L", "Mustafa", "Jonas_H", "Li_Na", "Marco_R",
		"Burak", "Anton", "Yusuf_07", "Chen_Hao", "Nico", "Karim", "Viktor_S", "Arda", "Liu_Yang", "Mathis",
		"Dmitri", "Kaan", "Wang_Lei", "Theo_D", "Pavel", "Serkan", "Hao_Ran", "Louis_V", "Rustam", "Baptiste"
	};

	//------------------------------------------------------------------------------------------------
	static SPVP_BotManagerComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (SCR_Global.IsEditMode())
			return;

		s_Instance = this;
		GetGame().GetCallqueue().CallLater(Tick, 2000, true);
		GetGame().GetCallqueue().CallLater(Brain, 1000, true);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Tick);
		GetGame().GetCallqueue().Remove(Brain);
		if (s_Instance == this)
			s_Instance = null;

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! Nombre de bots voulu pour ce nombre de joueurs (paliers I03, plafond I05)
	int GetCible(int joueurs)
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		if (!settings.m_bBotsActifs || joueurs <= 0)
			return 0;

		int cible = 0;
		if (joueurs <= settings.m_iPalier1Joueurs)
			cible = settings.m_iPalier1Bots;
		else if (joueurs <= settings.m_iPalier2Joueurs)
			cible = settings.m_iPalier2Bots;
		else if (joueurs <= settings.m_iPalier3Joueurs)
			cible = settings.m_iPalier3Bots;

		return Math.Min(cible, settings.m_iBotsMax);
	}

	//================================================================================================
	// Gestion des emplacements (toutes les 2 s)
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		if (!Replication.IsServer())
			return;

		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (!rounds)
			return;

		SPVP_Settings settings = SPVP_Settings.Get();
		int joueurs = GetGame().GetPlayerManager().GetPlayerCount();

		// Serveur vide, bots coupés, ou manche arrêtée : aucun bot (I08, fin de manche)
		if (joueurs == 0 || !settings.m_bBotsActifs || rounds.GetEtat() != SPVP_EEtat.EN_COURS)
		{
			if (!m_aSlots.IsEmpty())
				DeleteAll();
			m_bDeploiementVu = false;
			return;
		}

		// Nouvelle manche
		if (rounds.GetManche() != m_iMancheVue)
		{
			m_iMancheVue = rounds.GetManche();
			m_bDeploiementVu = false;
			if (!m_aSlots.IsEmpty())
				DeleteAll();
		}

		// I08 : on attend qu'un vrai joueur soit en ville
		if (settings.m_bBotsAttendreDeploiement && !m_bDeploiementVu)
		{
			m_bDeploiementVu = AnyPlayerInCity();
			if (!m_bDeploiementVu)
				return;
		}

		AdjustSlots(GetCible(joueurs + settings.m_iBotsJoueursFictifs));
		HandleRespawns();
	}

	//------------------------------------------------------------------------------------------------
	protected bool AnyPlayerInCity()
	{
		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (!info.m_bBot && info.m_eLieu == SPVP_ELieu.VILLE)
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Ajoute un bot (un toutes les 3 s) ou marque les bots en trop comme « partants » (I07)
	protected void AdjustSlots(int cible)
	{
		int actifs = 0;
		foreach (int id : m_aSlots)
		{
			SPVP_PlayerInfo info = SPVP_Players.Find(id);
			if (info && !info.m_bPartant)
				actifs++;
		}

		if (actifs < cible)
		{
			// D'abord, garder un partant
			foreach (int partantId : m_aSlots)
			{
				SPVP_PlayerInfo partant = SPVP_Players.Find(partantId);
				if (partant && partant.m_bPartant)
				{
					partant.m_bPartant = false;
					return;
				}
			}

			float now = GetGame().GetWorld().GetWorldTime();
			if (now < m_fProchaineCreation)
				return;

			m_fProchaineCreation = now + 3000;
			CreateSlot();
			return;
		}

		if (actifs > cible)
		{
			// Le bot qui a le moins de kills part en premier
			SPVP_PlayerInfo choisi;
			foreach (int botId : m_aSlots)
			{
				SPVP_PlayerInfo candidat = SPVP_Players.Find(botId);
				if (!candidat || candidat.m_bPartant)
					continue;
				if (!choisi || candidat.m_iKills < choisi.m_iKills)
					choisi = candidat;
			}
			if (choisi)
				choisi.m_bPartant = true;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void CreateSlot()
	{
		int id = m_iProchainId;
		m_iProchainId--;

		SPVP_PlayerInfo info = SPVP_Players.Get(id);
		info.m_bBot = true;
		info.m_sNom = PickName();
		info.m_eLieu = SPVP_ELieu.AUCUN;
		info.m_fReapparition = GetGame().GetWorld().GetWorldTime();	// tout de suite
		m_aSlots.Insert(id);

		SPVP_Log.Info("Bot ajouté : " + info.m_sNom);
	}

	//------------------------------------------------------------------------------------------------
	protected string PickName()
	{
		array<string> libres = {};
		foreach (string nom : s_aNoms)
		{
			if (m_aNomsPris.Find(nom) < 0)
				libres.Insert(nom);
		}

		string choisi;
		if (libres.IsEmpty())
			choisi = "Bot_" + Math.RandomInt(100, 999).ToString();
		else
			choisi = libres.GetRandomElement();

		m_aNomsPris.Insert(choisi);
		return "[BOT] " + choisi;
	}

	//------------------------------------------------------------------------------------------------
	//! Morts : réapparition après le délai des joueurs, ou fermeture de l'emplacement s'il est partant
	protected void HandleRespawns()
	{
		float now = GetGame().GetWorld().GetWorldTime();
		int delai = SPVP_Settings.Get().m_iDelaiReapparition * 1000;

		for (int i = m_aSlots.Count() - 1; i >= 0; i--)
		{
			SPVP_PlayerInfo info = SPVP_Players.Find(m_aSlots[i]);
			if (!info)
			{
				m_aSlots.Remove(i);
				continue;
			}

			if (SPVP_Players.GetAliveEntity(info))
				continue;

			// Mort (ou jamais apparu)
			if (info.m_bPartant)
			{
				CloseSlot(i);
				continue;
			}

			if (info.m_fReapparition <= 0)
			{
				info.m_fReapparition = now + delai;
				continue;
			}

			if (now >= info.m_fReapparition)
				Spawn(info);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void CloseSlot(int index)
	{
		SPVP_PlayerInfo info = SPVP_Players.Find(m_aSlots[index]);
		if (info)
		{
			DeleteBot(info);
			SPVP_Log.Info("Bot retiré : " + info.m_sNom);

			// Le nom redevient libre (PickName ajoute « [BOT] » seulement au nom affiché)
			string nom = info.m_sNom;
			if (nom.StartsWith("[BOT] "))
				nom = nom.Substring(6, nom.Length() - 6);
			m_aNomsPris.RemoveItem(nom);
			SPVP_Players.Remove(info.m_iPlayerId);
		}
		m_aSlots.Remove(index);
	}

	//------------------------------------------------------------------------------------------------
	//! Tous les bots retirés (serveur vide, fin de manche)
	protected void DeleteAll()
	{
		for (int i = m_aSlots.Count() - 1; i >= 0; i--)
		{
			CloseSlot(i);
		}
		m_aNomsPris.Clear();
	}

	//------------------------------------------------------------------------------------------------
	protected void DeleteBot(SPVP_PlayerInfo info)
	{
		if (info.m_Bot && !info.m_Bot.IsDeleted() && !SPVP_Players.IsDead(info.m_Bot))
			SCR_EntityHelper.DeleteEntityAndChildren(info.m_Bot);
		if (info.m_Groupe && !info.m_Groupe.IsDeleted())
			SCR_EntityHelper.DeleteEntityAndChildren(info.m_Groupe);

		info.m_Bot = null;
		info.m_Groupe = null;
		info.m_eLieu = SPVP_ELieu.AUCUN;
	}

	//================================================================================================
	// Apparition d'un bot
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	protected void Spawn(SPVP_PlayerInfo info)
	{
		info.m_fReapparition = 0;

		if (!m_aSoldats || m_aSoldats.IsEmpty() || m_sGroupPrefab.IsEmpty())
		{
			SPVP_Log.Error("Bots : aucune liste de soldats ou de groupe sur SPVP_BotManagerComponent (mode de jeu)");
			return;
		}

		Faction faction = GetGame().GetFactionManager().GetFactionByKey(m_sFactionKey);
		if (!faction)
		{
			SPVP_Log.Error("Bots : faction " + m_sFactionKey + " introuvable");
			return;
		}

		// Ancien groupe (le jeu le supprime quand il est vide, sinon on s'en charge)
		if (info.m_Groupe && !info.m_Groupe.IsDeleted())
			SCR_EntityHelper.DeleteEntityAndChildren(info.m_Groupe);
		info.m_Groupe = null;

		vector angles;
		vector position = SPVP_SpawnPicker.PickInCity(info.m_iPlayerId, angles);

		IEntity character = SpawnAt(m_aSoldats.GetRandomElement(), position, angles);
		if (!character)
			return;

		SCR_AIGroup group = SCR_AIGroup.Cast(SpawnAt(m_sGroupPrefab, position, vector.Zero));
		if (!group)
		{
			SCR_EntityHelper.DeleteEntityAndChildren(character);
			return;
		}

		group.SetFaction(faction);
		group.AddAgentFromControlledEntity(character);

		AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
		if (control)
		{
			control.ActivateAI();
			AIAgent agent = control.GetControlAIAgent();
			if (agent)
				agent.SetPermanentLOD(0);	// IA jamais ralentie, même quand tous les joueurs sont loin
		}

		SPVP_Combat.OnSpawnedInCity(character);

		info.m_Bot = character;
		info.m_Groupe = group;
		info.m_eLieu = SPVP_ELieu.VILLE;
		info.m_iHorsZone = 0;
		info.m_vDernierePosition = position;
		info.m_iImmobile = 0;
		SPVP_Settings settings = SPVP_Settings.Get();
		info.m_fProchainIndice = GetGame().GetWorld().GetWorldTime() + Math.RandomFloat(settings.m_iBotsIndiceMin, settings.m_iBotsIndiceMax) * 1000;

		// Réglages CRX maintenant, puis 2 s plus tard (CRX recharge ses valeurs globales quand le soldat rejoint le groupe)
		SPVP_BotAI.Configure(group, character);
		GetGame().GetCallqueue().CallLater(SPVP_BotAI.Configure, 2000, false, group, character);
	}

	//------------------------------------------------------------------------------------------------
	protected IEntity SpawnAt(ResourceName prefab, vector position, vector angles)
	{
		if (prefab.IsEmpty())
			return null;

		Resource res = Resource.Load(prefab);
		if (!res || !res.IsValid())
		{
			SPVP_Log.Error("Bots : prefab invalide " + prefab);
			return null;
		}

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		Math3D.AnglesToMatrix(Vector(angles[1], angles[0], angles[2]), params.Transform);
		params.Transform[3] = position;
		return GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
	}

	//================================================================================================
	// Directeur : ordres aux bots inoccupés (toutes les secondes)
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	protected void Brain()
	{
		if (!Replication.IsServer() || m_aSlots.IsEmpty())
			return;

		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		if (!rounds || rounds.GetEtat() != SPVP_EEtat.EN_COURS)
			return;

		vector center = rounds.GetCentre();
		float rayon = rounds.GetRayon();
		float now = GetGame().GetWorld().GetWorldTime();
		SPVP_Settings settings = SPVP_Settings.Get();

		foreach (int id : m_aSlots)
		{
			SPVP_PlayerInfo info = SPVP_Players.Find(id);
			IEntity bot = SPVP_Players.GetAliveEntity(info);
			if (!bot)
				continue;

			vector position = bot.GetOrigin();
			bool enCombat = SPVP_BotAI.HasTarget(bot);

			// Immobile sans combattre ?
			if (vector.DistanceXZ(position, info.m_vDernierePosition) < 1 && !enCombat)
				info.m_iImmobile++;
			else
				info.m_iImmobile = 0;
			info.m_vDernierePosition = position;

			// Près du bord ou dehors : retour vers l'intérieur (V11)
			if (vector.DistanceXZ(position, center) > rayon - BORD_ZONE)
			{
				if (!enCombat || vector.DistanceXZ(position, center) > rayon)
				{
					SPVP_BotAI.Investigate(info.m_Groupe, bot, RandomPointInCircle(center, rayon * 0.5), 20, 30);
					info.m_iImmobile = 0;
				}
				continue;
			}

			if (enCombat)
				continue;

			// I14 : un secteur vague où se trouve quelqu'un, toutes les 60 à 120 s (ou tout de suite s'il est bloqué)
			if (now >= info.m_fProchainIndice || info.m_iImmobile >= IMMOBILE_MAX)
			{
				vector cible;
				if (!PickHint(info, center, rayon, cible))
					cible = RandomPointInCircle(center, rayon * 0.8);

				SPVP_BotAI.Investigate(info.m_Groupe, bot, cible, 25, 60);
				info.m_fProchainIndice = now + Math.RandomFloat(settings.m_iBotsIndiceMin, settings.m_iBotsIndiceMax) * 1000;
				info.m_iImmobile = 0;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Un combattant vivant en ville (joueurs deux fois plus souvent), position floutée dans un secteur d'environ 100 m
	protected bool PickHint(SPVP_PlayerInfo self, vector center, float rayon, out vector point)
	{
		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);

		array<IEntity> tirage = {};
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (info == self || info.m_eLieu != SPVP_ELieu.VILLE)
				continue;

			IEntity entity = SPVP_Players.GetAliveEntity(info);
			if (!entity || vector.DistanceXZ(entity.GetOrigin(), center) > rayon)
				continue;

			tirage.Insert(entity);
			if (!info.m_bBot)
				tirage.Insert(entity);	// poids double pour les vrais joueurs
		}

		if (tirage.IsEmpty())
			return false;

		IEntity choisi = tirage.GetRandomElement();
		float flou = SPVP_Settings.Get().m_fBotsIndiceRayon;
		point = choisi.GetOrigin();
		float angle = Math.RandomFloat(0, Math.PI2);
		float distance = Math.RandomFloat(0, flou);
		point[0] = point[0] + Math.Cos(angle) * distance;
		point[2] = point[2] + Math.Sin(angle) * distance;

		// Ramené dans le cercle
		vector ecart = point - center;
		ecart[1] = 0;
		float longueur = ecart.Length();
		float max = rayon - 15;
		if (longueur > max && longueur > 0)
		{
			point[0] = center[0] + ecart[0] / longueur * max;
			point[2] = center[2] + ecart[2] / longueur * max;
		}
		point[1] = SCR_TerrainHelper.GetTerrainY(point);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected vector RandomPointInCircle(vector center, float rayon)
	{
		float angle = Math.RandomFloat(0, Math.PI2);
		float distance = rayon * Math.Sqrt(Math.RandomFloat(0, 1));
		vector point = center;
		point[0] = center[0] + Math.Cos(angle) * distance;
		point[2] = center[2] + Math.Sin(angle) * distance;
		point[1] = SCR_TerrainHelper.GetTerrainY(point);
		return point;
	}
}
