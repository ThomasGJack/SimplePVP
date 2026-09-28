//------------------------------------------------------------------------------------------------
// SimplePVP — Chef d'orchestre des manches (à poser sur le mode de jeu)
// ATTENTE (personne, ou serveur non autorisé)
//   → première manche : ville au hasard, sans vote ni gel (P04, P05)
// EN_COURS (15 min dans une ville)
//   → FIN : résultats, tout le monde revient au lobby (10 s) (P06, D03, L08)
//   → VOTE : 3 villes proposées, on vote à l'ordinateur du lobby, grand nettoyage (15 s) (V16, V17, AN03)
//   → DEPART : ville fixée, météo, portes ; ceux qui ont demandé à partir apparaissent ensemble, figés 5 s (DN04, R3)
//   → EN_COURS (GO)…
// Gère aussi : zone de jeu (mort après 10 s dehors), classement répliqué, annonces, fil des éliminations,
// bandeaux, résultats (podium, médailles), joueurs partis et revenus (PN05, PN06).
// Le calcul des kills et des points est dans SPVP_Score.
// Livraisons 1, 2 et 4
//------------------------------------------------------------------------------------------------

enum SPVP_EEtat
{
	ATTENTE,
	EN_COURS,
	FIN,
	VOTE,
	DEPART
}

//! Réponse à une demande de déploiement depuis le lobby
enum SPVP_EDeploiement
{
	MAINTENANT,
	FILE,			// mis en file : départ avec la prochaine manche (R3)
	REFUSE
}

[ComponentEditorProps(category: "SimplePVP", description: "Manches, rotation des villes, vote, zone de jeu et scores de SimplePVP")]
class SPVP_RoundManagerComponentClass : SCR_BaseGameModeComponentClass
{
}

class SPVP_RoundManagerComponent : SCR_BaseGameModeComponent
{
	// --- Répliqué vers les joueurs (affichage)
	[RplProp()]
	protected int m_iEtat;
	[RplProp()]
	protected int m_iRestant;			// secondes restantes de l'étape en cours
	[RplProp()]
	protected int m_iManche;			// numéro de la manche
	[RplProp()]
	protected string m_sVille;
	[RplProp()]
	protected vector m_vCentre;
	[RplProp()]
	protected float m_fRayon;
	[RplProp()]
	protected bool m_bNuit;
	[RplProp()]
	protected bool m_bAutorise = true;	// S23
	[RplProp()]
	protected int m_iBlocageFin = 30;	// L07, copié des réglages pour que le joueur voie la même règle
	[RplProp()]
	protected ref array<int> m_aIds = {};		// combattants (joueurs > 0, bots < 0), dans l'ordre du classement
	[RplProp()]
	protected ref array<int> m_aKills = {};
	[RplProp()]
	protected ref array<int> m_aMorts = {};
	[RplProp()]
	protected ref array<int> m_aLieux = {};		// SPVP_ELieu de chaque combattant, ou LIEU_FILE s'il attend le départ
	[RplProp()]
	protected ref array<string> m_aNoms = {};		// noms affichés
	[RplProp()]
	protected ref array<int> m_aPoints = {};
	[RplProp()]
	protected string m_sResultats;					// fin de manche : lignes « V|… », « P|… », « M|… » (P20)
	[RplProp()]
	protected ref array<string> m_aVoteNoms = {};	// villes proposées au vote
	[RplProp()]
	protected ref array<int> m_aVoteVoix = {};

	static const int LIEU_FILE = 10;

	protected static SPVP_RoundManagerComponent s_Instance;

	// --- Serveur
	protected ref array<int> m_aHistorique = {};	// index des dernières villes jouées
	protected ref array<int> m_aVoteVilles = {};	// index des villes proposées
	protected ref map<int, int> m_mVotes = new map<int, int>();	// joueur → choix
	protected string m_sGroupeCourant;
	protected int m_iCompteurOrdre;
	protected bool m_bServeurLance;
	protected bool m_bErreurVilleSignalee;
	protected int m_iLeaderPrecedent;
	protected ref map<string, ref SPVP_PlayerInfo> m_mAbsents = new map<string, ref SPVP_PlayerInfo>();	// PN05 : identité → score
	protected ref map<string, float> m_mAbsentsHeure = new map<string, float>();
	protected ref map<string, int> m_mAbsentsManche = new map<string, int>();

	protected static const int HISTORIQUE_MAX = 3;

	//------------------------------------------------------------------------------------------------
	static SPVP_RoundManagerComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (SCR_Global.IsEditMode())
			return;

		// Tout de suite : les statiques survivent d'une partie d'essai à l'autre dans Workbench, et le premier
		// joueur arrive avant la première seconde
		SPVP_Settings.Reset();
		SPVP_Players.Reset();
		SPVP_World.Reset();
		SPVP_Combat.Reset();
		SPVP_HitTracker.Reset();

		s_Instance = this;
		GetGame().GetCallqueue().CallLater(StartServer, 1000, false);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(StartServer);
		GetGame().GetCallqueue().Remove(Tick);
		if (s_Instance == this)
			s_Instance = null;

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	protected void StartServer()
	{
		if (!Replication.IsServer() || m_bServeurLance)
			return;

		m_bServeurLance = true;
		SPVP_Settings settings = SPVP_Settings.Get();
		m_iBlocageFin = settings.m_iBlocageFin;
		SPVP_World.Init();

		m_bAutorise = SPVP_World.IsAuthorized();
		if (!m_bAutorise)
			SPVP_Log.Error("Serveur non autorisé : clé « cle_serveur » absente ou fausse dans reglages.json. Aucune manche ne sera lancée.");

		m_iEtat = SPVP_EEtat.ATTENTE;
		m_sVille = "";
		Replication.BumpMe();

		GetGame().GetCallqueue().CallLater(Tick, 1000, true);
		SPVP_Log.Info("Gestionnaire de manches lancé");
	}

	//================================================================================================
	// Lecture (serveur et joueurs)
	//================================================================================================

	int GetEtat()
	{
		return m_iEtat;
	}

	int GetRestant()
	{
		return m_iRestant;
	}

	int GetManche()
	{
		return m_iManche;
	}

	string GetVille()
	{
		return m_sVille;
	}

	vector GetCentre()
	{
		return m_vCentre;
	}

	float GetRayon()
	{
		return m_fRayon;
	}

	bool IsNuit()
	{
		return m_bNuit;
	}

	bool IsAutorise()
	{
		return m_bAutorise;
	}

	//------------------------------------------------------------------------------------------------
	//! La ville est-elle ouverte aux apparitions (manche en cours ou départ groupé) ?
	bool IsVilleOuverte()
	{
		return m_iEtat == SPVP_EEtat.EN_COURS || m_iEtat == SPVP_EEtat.DEPART;
	}

	//------------------------------------------------------------------------------------------------
	//! Demande de déploiement depuis le lobby : tout de suite, en file pour la prochaine manche, ou refusée
	int CanDeployFromLobby(out string raison)
	{
		switch (m_iEtat)
		{
			case SPVP_EEtat.ATTENTE:
			{
				raison = "Manche en préparation";
				if (!m_bAutorise)
					raison = "Serveur non autorisé";
				return SPVP_EDeploiement.REFUSE;
			}

			case SPVP_EEtat.EN_COURS:
			{
				if (m_iRestant > m_iBlocageFin)
					return SPVP_EDeploiement.MAINTENANT;

				// L07 : trop tard pour cette manche, départ avec la suivante
				raison = string.Format("Fin de manche dans %1 : tu partiras avec la prochaine", FormatTemps(m_iRestant));
				return SPVP_EDeploiement.FILE;
			}
		}

		raison = "Tu partiras au départ de la prochaine manche";
		return SPVP_EDeploiement.FILE;
	}

	//------------------------------------------------------------------------------------------------
	//! Chez le joueur : son personnage est-il figé (départ groupé, DN04) ?
	static bool IsLocalFrozen()
	{
		SPVP_RoundManagerComponent rounds = GetInstance();
		if (!rounds || rounds.m_iEtat != SPVP_EEtat.DEPART)
			return false;

		PlayerController pc = GetGame().GetPlayerController();
		if (!pc)
			return false;

		IEntity entity = pc.GetControlledEntity();
		if (!entity)
			return false;

		float limite = rounds.m_fRayon + 50;
		return vector.DistanceSqXZ(entity.GetOrigin(), rounds.m_vCentre) <= limite * limite;
	}

	//------------------------------------------------------------------------------------------------
	//! Classement répliqué : kills, morts et lieu d'un combattant (false s'il n'est pas dans la liste)
	bool GetScore(int playerId, out int kills, out int morts, out int rang, out int lieu)
	{
		int index = m_aIds.Find(playerId);
		if (index < 0)
			return false;

		kills = m_aKills[index];
		morts = m_aMorts[index];
		rang = index + 1;
		lieu = m_aLieux[index];
		return true;
	}

	//------------------------------------------------------------------------------------------------
	int GetClassementCount()
	{
		return m_aIds.Count();
	}

	//------------------------------------------------------------------------------------------------
	void GetLigne(int index, out int playerId, out int kills, out int morts)
	{
		playerId = m_aIds[index];
		kills = m_aKills[index];
		morts = m_aMorts[index];
	}

	//------------------------------------------------------------------------------------------------
	int GetPoints(int index)
	{
		if (index < 0 || index >= m_aPoints.Count())
			return 0;
		return m_aPoints[index];
	}

	//------------------------------------------------------------------------------------------------
	int GetLieu(int index)
	{
		if (index < 0 || index >= m_aLieux.Count())
			return SPVP_ELieu.AUCUN;
		return m_aLieux[index];
	}

	//------------------------------------------------------------------------------------------------
	//! Résultats de la dernière manche (vide pendant la manche)
	string GetResultats()
	{
		return m_sResultats;
	}

	//------------------------------------------------------------------------------------------------
	//! Le 1er du classement parmi les vrais joueurs, s'il a au moins 1 kill (prime, bandeau leader), sinon 0
	int GetLeaderId()
	{
		for (int i = 0; i < m_aIds.Count(); i++)
		{
			if (m_aIds[i] <= 0)
				continue;
			if (m_aKills[i] > 0)
				return m_aIds[i];
			return 0;
		}
		return 0;
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur : numéro d'arrivée pour départager deux égalités (le plus petit est arrivé le premier)
	int NextOrdre()
	{
		m_iCompteurOrdre++;
		return m_iCompteurOrdre;
	}

	//------------------------------------------------------------------------------------------------
	string GetNom(int index)
	{
		if (index < 0 || index >= m_aNoms.Count())
			return "";
		return m_aNoms[index];
	}

	//------------------------------------------------------------------------------------------------
	int GetVoteCount()
	{
		return m_aVoteNoms.Count();
	}

	//------------------------------------------------------------------------------------------------
	string GetVoteNom(int choix)
	{
		if (choix < 0 || choix >= m_aVoteNoms.Count())
			return "";
		return m_aVoteNoms[choix];
	}

	//------------------------------------------------------------------------------------------------
	int GetVoteVoix(int choix)
	{
		if (choix < 0 || choix >= m_aVoteVoix.Count())
			return 0;
		return m_aVoteVoix[choix];
	}

	//------------------------------------------------------------------------------------------------
	static string FormatTemps(int secondes)
	{
		if (secondes < 0)
			secondes = 0;

		int minutes = secondes / 60;
		int reste = secondes % 60;
		string texteSecondes = reste.ToString();
		if (reste < 10)
			texteSecondes = "0" + texteSecondes;
		return minutes.ToString() + ":" + texteSecondes;
	}

	//================================================================================================
	// Serveur : horloge
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	protected void Tick()
	{
		if (!Replication.IsServer())
			return;

		int joueurs = GetGame().GetPlayerManager().GetPlayerCount();
		if (joueurs == 0 && m_iEtat != SPVP_EEtat.ATTENTE)
			StopToWaiting();

		SPVP_Settings settings = SPVP_Settings.Get();

		switch (m_iEtat)
		{
			case SPVP_EEtat.ATTENTE:
			{
				// P04 : un seul joueur suffit ; la première manche part tout de suite, ville au hasard
				if (joueurs > 0 && m_bAutorise)
				{
					int premiere = PickRandomCity(joueurs);
					if (PrepareArena(premiere, joueurs))
						Go();
				}
				break;
			}

			case SPVP_EEtat.EN_COURS:
			{
				m_iRestant--;
				CheckZone();
				SPVP_Combat.Tick();
				SPVP_Lobby.GuardLobby();

				if (m_iRestant <= 0)
					EndRound();
				break;
			}

			case SPVP_EEtat.FIN:
			{
				m_iRestant--;
				SPVP_Lobby.GuardLobby();
				if (m_iRestant <= 0)
					StartVote(joueurs);
				break;
			}

			case SPVP_EEtat.VOTE:
			{
				m_iRestant--;
				SPVP_Lobby.GuardLobby();
				if (m_iRestant <= 0)
					CloseVote(joueurs);
				break;
			}

			case SPVP_EEtat.DEPART:
			{
				m_iRestant--;
				if (m_iRestant <= 0)
					Go();
				break;
			}
		}

		RefreshClassement();
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	//! Ville fixée : nom, centre, taille de zone selon les joueurs, météo, portes (V09, V21-V23, C19)
	protected bool PrepareArena(int index, int joueurs)
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		if (index < 0 || index >= settings.m_aVilles.Count())
		{
			if (!m_bErreurVilleSignalee)
				SPVP_Log.Error("Aucune ville active dans reglages.json : impossible de lancer une manche");
			m_bErreurVilleSignalee = true;
			m_iEtat = SPVP_EEtat.ATTENTE;
			m_iRestant = 0;
			m_sVille = "";
			return false;
		}
		m_bErreurVilleSignalee = false;

		SPVP_Arena arena = settings.m_aVilles[index];
		m_aHistorique.Insert(index);
		while (m_aHistorique.Count() > HISTORIQUE_MAX)
		{
			m_aHistorique.RemoveOrdered(0);
		}

		m_iManche++;
		m_sVille = arena.m_sNom;
		m_sGroupeCourant = arena.GetGroupe();
		m_vCentre = arena.GetCenter();
		m_fRayon = arena.m_fRayonPetit;
		if (joueurs >= settings.m_iSeuilGrandeZone)
			m_fRayon = arena.m_fRayonGrand;

		m_bNuit = SPVP_World.PrepareWeather();
		SPVP_World.OpenDoors(m_vCentre, m_fRayon);

		// Scores de la manche remis à zéro
		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);
		foreach (SPVP_PlayerInfo info : infos)
		{
			info.ResetManche();
		}
		m_iCompteurOrdre = 0;
		m_iLeaderPrecedent = 0;
		m_sResultats = "";
		m_mAbsents.Clear();
		m_mAbsentsHeure.Clear();
		m_mAbsentsManche.Clear();
		SPVP_HitTracker.Reset();

		string moment = "de jour";
		if (m_bNuit)
			moment = "de nuit";
		SPVP_Log.Info(string.Format("Manche %1 : %2, zone de %3 m de rayon, %4", m_iManche, m_sVille, m_fRayon, moment));
		return true;
	}

	//------------------------------------------------------------------------------------------------
	//! GO : la manche part (15 min) ; ceux qui attendaient apparaissent en ville
	protected void Go()
	{
		m_iEtat = SPVP_EEtat.EN_COURS;
		m_iRestant = SPVP_Settings.Get().m_iDureeManche;
		SPVP_SpawnLogic.DeployQueued();
		Annonce("GO !", string.Format("%1 — %2. Ordinateur du lobby pour te déployer.", m_sVille, FormatTemps(m_iRestant)));
	}

	//------------------------------------------------------------------------------------------------
	//! Fin des 15 minutes (P06 : arrêt net, plus personne ne peut mourir) : vainqueur, retour de tous au lobby
	protected void EndRound()
	{
		m_iEtat = SPVP_EEtat.FIN;
		m_iRestant = SPVP_Settings.Get().m_iDureeResultats;

		// Vainqueur : le plus de kills, puis le moins de morts, puis le premier arrivé (P02, P09)
		RefreshClassement();
		BuildResults();
		string texte = "Pas de vainqueur";
		array<string> lignes = {};
		m_sResultats.Split("\n", lignes, true);
		foreach (string ligne : lignes)
		{
			array<string> champs = {};
			ligne.Split("|", champs, false);
			if (champs.Count() >= 3 && champs[0] == "V")
				texte = string.Format("Vainqueur : %1 (%2 kills)", champs[1], champs[2]);
		}
		SPVP_Log.Info(string.Format("Fin de la manche %1 à %2. %3", m_iManche, m_sVille, texte));
		Annonce("Fin de la manche", texte);

		// D03 : tout le monde revient au lobby, avec son dernier équipement
		SPVP_SpawnLogic.SendEveryoneToLobby();
	}

	//------------------------------------------------------------------------------------------------
	//! P20, I23 : vainqueur (meilleur vrai joueur, au moins N kills), podium des 3 meilleurs vrais joueurs, médailles
	//! Lignes : « V|nom|kills|morts|points », « P|rang|nom|kills|morts|points », « M|code|nom|valeur »
	protected void BuildResults()
	{
		int killsMin = SPVP_Settings.Get().m_iKillsMinVictoire;
		string texte = "";
		int podium = 0;
		for (int i = 0; i < m_aIds.Count() && podium < 3; i++)
		{
			if (m_aIds[i] <= 0)
				continue;

			if (podium == 0 && m_aKills[i] >= killsMin && m_aKills[i] > 0)
				texte += string.Format("V|%1|%2|%3|%4\n", m_aNoms[i], m_aKills[i], m_aMorts[i], m_aPoints[i]);

			podium++;
			texte += string.Format("P|%1|%2|%3|%4|%5\n", podium, m_aNoms[i], m_aKills[i], m_aMorts[i], m_aPoints[i]);
		}

		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);
		array<SPVP_PlayerInfo> presents = {};
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (!info.m_bBot && GetGame().GetPlayerManager().IsPlayerConnected(info.m_iPlayerId))
				presents.Insert(info);
		}

		array<string> medailles = {};
		SPVP_Score.Medals(presents).Split("\n", medailles, true);
		foreach (string medaille : medailles)
		{
			texte += "M|" + medaille + "\n";
		}

		m_sResultats = texte;
	}

	//------------------------------------------------------------------------------------------------
	protected void StopToWaiting()
	{
		m_iEtat = SPVP_EEtat.ATTENTE;
		m_iRestant = 0;
		m_sVille = "";
		m_aVoteNoms.Clear();
		m_aVoteVoix.Clear();
		m_mVotes.Clear();
		SPVP_World.CleanAll();
		SPVP_Log.Info("Serveur vide : manche arrêtée, en attente de joueurs");
	}

	//================================================================================================
	// Serveur : vote de la ville suivante (V16, V17, V18)
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	protected void StartVote(int joueurs)
	{
		m_iEtat = SPVP_EEtat.VOTE;
		m_iRestant = SPVP_Settings.Get().m_iDureeVote;

		// Ville propre pour la suite (AN03, C21)
		SPVP_World.CleanAll();

		PickCandidates(joueurs);
		m_mVotes.Clear();
		m_aVoteNoms.Clear();
		m_aVoteVoix.Clear();
		SPVP_Settings settings = SPVP_Settings.Get();
		foreach (int index : m_aVoteVilles)
		{
			m_aVoteNoms.Insert(settings.m_aVilles[index].m_sNom);
			m_aVoteVoix.Insert(0);
		}

		string liste = "";
		foreach (string nom : m_aVoteNoms)
		{
			if (!liste.IsEmpty())
				liste += ", ";
			liste += nom;
		}
		Annonce("Vote de la ville suivante", liste + ". Vote à l'ordinateur du lobby.");
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur : un joueur vote (changer d'avis est permis)
	void Vote(int playerId, int choix)
	{
		if (m_iEtat != SPVP_EEtat.VOTE || choix < 0 || choix >= m_aVoteVilles.Count())
			return;

		m_mVotes.Set(playerId, choix);
		CountVotes();
		Replication.BumpMe();
		SPVP_Notify.ToPlayer(playerId, "Vote", "Tu as voté pour " + m_aVoteNoms[choix], 3);
	}

	//------------------------------------------------------------------------------------------------
	protected void CountVotes()
	{
		for (int i = 0; i < m_aVoteVoix.Count(); i++)
		{
			m_aVoteVoix[i] = 0;
		}

		foreach (int votant, int choix : m_mVotes)
		{
			if (!GetGame().GetPlayerManager().IsPlayerConnected(votant))
				continue;
			if (choix >= 0 && choix < m_aVoteVoix.Count())
				m_aVoteVoix[choix] = m_aVoteVoix[choix] + 1;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Fin du vote : la ville qui a le plus de voix (égalité ou aucun vote : au hasard), puis départ groupé
	protected void CloseVote(int joueurs)
	{
		CountVotes();

		int index = -1;
		if (!m_aVoteVilles.IsEmpty())
		{
			int meilleur = -1;
			array<int> ex = {};
			for (int i = 0; i < m_aVoteVoix.Count(); i++)
			{
				if (m_aVoteVoix[i] > meilleur)
				{
					meilleur = m_aVoteVoix[i];
					ex.Clear();
				}
				if (m_aVoteVoix[i] == meilleur)
					ex.Insert(i);
			}
			index = m_aVoteVilles[ex.GetRandomElement()];
		}
		else
		{
			index = PickRandomCity(joueurs);
		}

		m_aVoteNoms.Clear();
		m_aVoteVoix.Clear();
		m_mVotes.Clear();

		if (!PrepareArena(index, joueurs))
			return;

		// DN04 : départ groupé, figé quelques secondes
		m_iRestant = SPVP_Settings.Get().m_iDureeDepart;
		if (m_iRestant <= 0)
		{
			Go();
			return;
		}

		m_iEtat = SPVP_EEtat.DEPART;
		SPVP_SpawnLogic.DeployQueued();
		Annonce("Prochaine manche : " + m_sVille, string.Format("Départ dans %1 s", m_iRestant));
	}

	//------------------------------------------------------------------------------------------------
	//! 3 villes : actives, dans la fourchette de joueurs, pas jouées récemment, pas deux du même groupe
	protected void PickCandidates(int joueurs)
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		m_aVoteVilles.Clear();

		array<int> pool = {};
		for (int i = 0; i < settings.m_aVilles.Count(); i++)
		{
			SPVP_Arena arena = settings.m_aVilles[i];
			if (arena.Accepte(joueurs) && m_aHistorique.Find(i) < 0 && arena.GetGroupe() != m_sGroupeCourant)
				pool.Insert(i);
		}
		AddCandidates(pool);

		// Pas assez : on réintègre les villes jouées récemment, puis on ignore la fourchette de joueurs
		if (m_aVoteVilles.Count() < 3)
		{
			pool.Clear();
			for (int j = 0; j < settings.m_aVilles.Count(); j++)
			{
				SPVP_Arena other = settings.m_aVilles[j];
				if (other.m_bActif && other.GetGroupe() != m_sGroupeCourant)
					pool.Insert(j);
			}
			AddCandidates(pool);
		}

		// Une seule ville (ou un seul groupe) active : on rejoue la même plutôt que de bloquer les manches
		if (m_aVoteVilles.IsEmpty())
		{
			pool.Clear();
			for (int k = 0; k < settings.m_aVilles.Count(); k++)
			{
				if (settings.m_aVilles[k].m_bActif)
					pool.Insert(k);
			}
			AddCandidates(pool);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AddCandidates(notnull array<int> pool)
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		while (m_aVoteVilles.Count() < 3 && !pool.IsEmpty())
		{
			int tire = pool.GetRandomIndex();
			int index = pool[tire];
			pool.Remove(tire);

			if (m_aVoteVilles.Find(index) >= 0)
				continue;

			// Jamais deux quartiers de la même ville au même vote
			bool memeGroupe = false;
			foreach (int deja : m_aVoteVilles)
			{
				if (settings.m_aVilles[deja].GetGroupe() == settings.m_aVilles[index].GetGroupe())
					memeGroupe = true;
			}
			if (!memeGroupe)
				m_aVoteVilles.Insert(index);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! Ville au hasard (première manche, vote impossible) : même règles que le vote
	protected int PickRandomCity(int joueurs)
	{
		PickCandidates(joueurs);
		if (m_aVoteVilles.IsEmpty())
			return -1;
		return m_aVoteVilles.GetRandomElement();
	}

	//================================================================================================
	// Serveur : zone
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	//! V13, V14 : hors du cercle, un compte à rebours ; à zéro, la mort
	protected void CheckZone()
	{
		SPVP_Settings settings = SPVP_Settings.Get();
		float rayonSq = m_fRayon * m_fRayon;

		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (info.m_eLieu != SPVP_ELieu.VILLE)
			{
				info.m_iHorsZone = 0;
				continue;
			}

			IEntity character = SPVP_Players.GetAliveEntity(info);
			if (!character)
			{
				info.m_iHorsZone = 0;
				continue;
			}

			if (vector.DistanceSqXZ(character.GetOrigin(), m_vCentre) <= rayonSq)
			{
				if (info.m_iHorsZone > 0 && !info.m_bBot)
					SPVP_Notify.ToPlayer(info.m_iPlayerId, "Zone de jeu", "Tu es revenu dans la zone.", 2);
				info.m_iHorsZone = 0;
				continue;
			}

			info.m_iHorsZone++;
			int reste = settings.m_iTempsHorsZone - info.m_iHorsZone;
			if (reste > 0)
			{
				if (!info.m_bBot)
					SPVP_Notify.ToPlayer(info.m_iPlayerId, "HORS ZONE", string.Format("Reviens dans la zone : mort dans %1 s", reste), 1.5);
				continue;
			}

			info.m_iHorsZone = 0;
			SCR_CharacterDamageManagerComponent damage = SCR_CharacterDamageManagerComponent.Cast(character.FindComponent(SCR_CharacterDamageManagerComponent));
			if (damage)
			{
				// Tué par lui-même : le dernier tireur des 30 s marque le kill (P11)
				SPVP_Log.Info(SPVP_Players.GetName(info) + " : mort hors zone");
				damage.Kill(Instigator.CreateInstigator(character));
			}
		}
	}

	//================================================================================================
	// Serveur : morts et scores
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	//! Toute mort de combattant (joueur ou bot)
	override void OnControllableDestroyed(notnull SCR_InstigatorContextData instigatorContextData)
	{
		super.OnControllableDestroyed(instigatorContextData);
		if (!Replication.IsServer())
			return;

		int victimId = instigatorContextData.GetVictimPlayerID();
		if (victimId <= 0)
			victimId = SPVP_Players.GetFighterId(instigatorContextData.GetVictimEntity());

		// Corps (30 au plus) et pièges du mort (C21), quel que soit le moment
		SPVP_World.OnFighterDied(instigatorContextData.GetVictimEntity(), victimId);

		if (m_iEtat != SPVP_EEtat.EN_COURS)
			return;

		SPVP_PlayerInfo victim = SPVP_Players.Find(victimId);
		if (!victim || victim.m_eLieu != SPVP_ELieu.VILLE)
			return;

		SPVP_Combat.OnDeath(instigatorContextData.GetVictimEntity());

		int killerId = instigatorContextData.GetKillerPlayerID();
		if (killerId <= 0)
			killerId = SPVP_Players.GetFighterId(instigatorContextData.GetKillerEntity());

		SPVP_Score.OnDeath(this, victim, instigatorContextData.GetVictimEntity(), killerId, instigatorContextData.GetKillerEntity());
		AfterScoreChange();
	}

	//------------------------------------------------------------------------------------------------
	//! Classement à jour, et bandeau « nouveau leader » si le 1er a changé (P25)
	protected void AfterScoreChange()
	{
		RefreshClassement();
		Replication.BumpMe();

		int leader = GetLeaderId();
		if (m_iEtat == SPVP_EEtat.EN_COURS && leader != 0 && leader != m_iLeaderPrecedent)
		{
			SPVP_PlayerInfo info = SPVP_Players.Find(leader);
			if (info)
				SendBanner(SPVP_EBandeau.LEADER, SPVP_Players.GetName(info), info.m_iKills);
		}
		m_iLeaderPrecedent = leader;
	}

	//------------------------------------------------------------------------------------------------
	//! Identité Bohemia du joueur (ou "" si inconnue)
	protected static string IdentityOf(int playerId)
	{
		string identite = SCR_PlayerIdentityUtils.GetPlayerIdentityId(playerId);
		string vide = UUID.NULL_UUID;
		if (identite == vide)
			return "";
		return identite;
	}

	//------------------------------------------------------------------------------------------------
	//! PN05 : un joueur revenu dans les 5 min pendant la même manche retrouve son score
	override void OnPlayerAuditSuccess(int playerId)
	{
		super.OnPlayerAuditSuccess(playerId);
		if (!Replication.IsServer())
			return;

		string identite = IdentityOf(playerId);
		if (identite.IsEmpty())
			return;

		SPVP_PlayerInfo ancienne;
		if (!m_mAbsents.Find(identite, ancienne))
			return;

		float heure = m_mAbsentsHeure.Get(identite);
		int manche = m_mAbsentsManche.Get(identite);
		m_mAbsents.Remove(identite);
		m_mAbsentsHeure.Remove(identite);
		m_mAbsentsManche.Remove(identite);

		float delai = SPVP_Settings.Get().m_iReconnexionMin * 60000;
		if (manche != m_iManche || GetGame().GetWorld().GetWorldTime() - heure > delai)
			return;

		SPVP_Players.Get(playerId).CopyScore(ancienne);
		SPVP_Log.Info(GetGame().GetPlayerManager().GetPlayerName(playerId) + " est revenu : score de la manche retrouvé");
		RefreshClassement();
		Replication.BumpMe();
	}

	//------------------------------------------------------------------------------------------------
	override void OnPlayerDisconnected(int playerId, KickCauseCode cause, int timeout)
	{
		super.OnPlayerDisconnected(playerId, cause, timeout);
		if (!Replication.IsServer())
			return;

		SPVP_World.RemoveTraps(playerId);
		m_mVotes.Remove(playerId);

		SPVP_PlayerInfo info = SPVP_Players.Find(playerId);
		if (info && m_iEtat == SPVP_EEtat.EN_COURS)
		{
			// PN06 : parti en pleine blessure = mort, kill au dernier tireur des 10 s
			if (info.m_eLieu == SPVP_ELieu.VILLE)
			{
				IEntity character = SPVP_Players.GetAliveCharacter(playerId);
				if (character)
					SPVP_Score.OnWoundedLeave(this, info, character);
			}

			// PN05 : score gardé quelques minutes
			string identite = IdentityOf(playerId);
			if (!identite.IsEmpty() && (info.m_iKills > 0 || info.m_iMorts > 0 || info.m_iPoints > 0))
			{
				m_mAbsents.Set(identite, info);
				m_mAbsentsHeure.Set(identite, GetGame().GetWorld().GetWorldTime());
				m_mAbsentsManche.Set(identite, m_iManche);
			}
		}

		SPVP_Players.Remove(playerId);
		AfterScoreChange();
	}

	//------------------------------------------------------------------------------------------------
	//! Classement : kills décroissants, puis morts croissantes, puis premier arrivé à son total
	protected void RefreshClassement()
	{
		array<SPVP_PlayerInfo> infos = {};
		SPVP_Players.GetAll(infos);

		array<SPVP_PlayerInfo> tri = {};
		foreach (SPVP_PlayerInfo info : infos)
		{
			if (!info.m_bBot && !GetGame().GetPlayerManager().IsPlayerConnected(info.m_iPlayerId))
				continue;

			int position = 0;
			while (position < tri.Count() && !Passe(info, tri[position]))
			{
				position++;
			}
			tri.InsertAt(info, position);
		}

		m_aIds.Clear();
		m_aKills.Clear();
		m_aMorts.Clear();
		m_aLieux.Clear();
		m_aNoms.Clear();
		m_aPoints.Clear();
		foreach (SPVP_PlayerInfo ligne : tri)
		{
			m_aPoints.Insert(ligne.m_iPoints);
			m_aNoms.Insert(SPVP_Players.GetName(ligne));
			m_aIds.Insert(ligne.m_iPlayerId);
			m_aKills.Insert(ligne.m_iKills);
			m_aMorts.Insert(ligne.m_iMorts);
			if (ligne.m_bEnFile)
				m_aLieux.Insert(LIEU_FILE);
			else
				m_aLieux.Insert(ligne.m_eLieu);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! a passe-t-il devant b ?
	protected bool Passe(SPVP_PlayerInfo a, SPVP_PlayerInfo b)
	{
		if (a.m_iKills != b.m_iKills)
			return a.m_iKills > b.m_iKills;
		if (a.m_iMorts != b.m_iMorts)
			return a.m_iMorts < b.m_iMorts;
		if (a.m_iKills > 0 && a.m_iOrdre != b.m_iOrdre)
			return a.m_iOrdre < b.m_iOrdre;
		return a.m_iPlayerId < b.m_iPlayerId;
	}

	//================================================================================================
	// Annonces à tous
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	protected void Annonce(string titre, string texte)
	{
		Rpc(RpcDo_Annonce, titre, texte);
		RpcDo_Annonce(titre, texte);	// joueur local de l'hôte (Workbench, partie hébergée)
	}

	//------------------------------------------------------------------------------------------------
	//! P23 : une ligne du fil des éliminations pour tous (tueur vide = mort seule)
	void SendKillFeed(string tueur, string arme, string victime, int flags, int distance)
	{
		Rpc(RpcDo_KillFeed, tueur, arme, victime, flags, distance);
		RpcDo_KillFeed(tueur, arme, victime, flags, distance);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_KillFeed(string tueur, string arme, string victime, int flags, int distance)
	{
		SPVP_HudComponent hud = SPVP_HudComponent.GetInstance();
		if (hud)
			hud.AddKillFeed(tueur, arme, victime, flags, distance);
	}

	//------------------------------------------------------------------------------------------------
	//! P25 : bandeau pour tous (série, nouveau leader)
	void SendBanner(int type, string nom, int valeur)
	{
		Rpc(RpcDo_Banner, type, nom, valeur);
		RpcDo_Banner(type, nom, valeur);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Banner(int type, string nom, int valeur)
	{
		SPVP_HudComponent hud = SPVP_HudComponent.GetInstance();
		if (hud)
			hud.ShowServerBanner(type, nom, valeur);
	}

	//------------------------------------------------------------------------------------------------
	[RplRpc(RplChannel.Reliable, RplRcver.Broadcast)]
	protected void RpcDo_Annonce(string titre, string texte)
	{
		if (System.IsConsoleApp())
			return;

		SCR_HintManagerComponent.ShowCustomHint(texte, titre, 8.0);
	}
}

//------------------------------------------------------------------------------------------------
//! DN04 : pendant le départ groupé, le joueur en ville ne peut ni bouger ni tirer (il peut regarder autour de lui).
//! Le jeu rappelle SetLocalControls à chaque image : le gel se lève tout seul au GO.
modded class SCR_BaseGameMode
{
	//------------------------------------------------------------------------------------------------
	override protected void SetLocalControls(bool enabled)
	{
		super.SetLocalControls(enabled && !SPVP_RoundManagerComponent.IsLocalFrozen());
	}
}
