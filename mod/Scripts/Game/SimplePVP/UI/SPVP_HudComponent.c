//------------------------------------------------------------------------------------------------
// SimplePVP — Affichage du joueur local, façon CoD
//   - en haut au centre : chrono · ville, puis tes kills · tes points · ton rang · le 1er (P21) ;
//   - bandeau sous le chrono : série, nouveau leader, 1 minute restante, 3-2-1-GO (P25, DN04) ;
//   - en haut à droite : fil des éliminations, 5 lignes, 6 s chacune (P23) ;
//   - sous le centre : popups de points « +100 Élimination » avec un son (P24) ;
//   - au centre : croix de touche, blanche, rouge au kill (C25) ;
//   - tableau des scores ouvert / fermé par la touche du tableau du jeu (P maintenu, ou View + Menu à la
//     manette), et résultats en fin de manche (P20).
// À poser sur le mode de jeu. Ne fait rien sur un serveur dédié.
// Livraisons 1 et 4
//------------------------------------------------------------------------------------------------

[ComponentEditorProps(category: "SimplePVP", description: "Affichage de SimplePVP (chrono, score, fil, popups, tableau)")]
class SPVP_HudComponentClass : SCR_BaseGameModeComponentClass
{
}

//------------------------------------------------------------------------------------------------
class SPVP_HudLine
{
	string m_sTexte;
	float m_fFin;		// heure (ms, horloge du jeu) où la ligne disparaît
}

//------------------------------------------------------------------------------------------------
class SPVP_HudComponent : SCR_BaseGameModeComponent
{
	[Attribute("{6A7494FD34783DDD}UI/layouts/SimplePVP/SPVP_Hud.layout", UIWidgets.ResourceNamePicker, "Mise en page de l'affichage", "layout")]
	protected ResourceName m_sLayout;

	static const string ORANGE = "255,106,19,255";
	static const string GRIS = "154,160,166,255";
	static const string BLANC = "242,242,242,255";
	static const string ROUGE = "230,40,40,255";

	protected static const int FIL_LIGNES = 5;
	protected static const float FIL_DUREE_MS = 6000;
	protected static const float POPUP_DUREE_MS = 2000;
	protected static const float BANDEAU_DUREE_MS = 3000;
	protected static const float CROIX_TOUCHE_MS = 150;
	protected static const float CROIX_KILL_MS = 400;
	protected static const int TABLEAU_LIGNES = 30;
	protected static const int NOM_MAX = 24;

	protected static SPVP_HudComponent s_Instance;

	protected Widget m_wRoot;
	protected TextWidget m_wLigne1;
	protected TextWidget m_wLigne2;
	protected RichTextWidget m_wBandeau;
	protected RichTextWidget m_wFil;
	protected RichTextWidget m_wPopups;
	protected TextWidget m_wCroix;
	protected Widget m_wTableau;
	protected RichTextWidget m_wTabTitre;
	protected RichTextWidget m_wTabPied;
	protected ref array<RichTextWidget> m_aColonnes = {};

	protected ref array<ref SPVP_HudLine> m_aFil = {};
	protected ref array<ref SPVP_HudLine> m_aPopups = {};
	protected string m_sBandeau;
	protected float m_fBandeauFin;
	protected ref array<string> m_aBandeauxEnAttente = {};
	protected ref array<string> m_aSonsEnAttente = {};
	protected float m_fCroixFin;
	protected bool m_bCroixRouge;
	protected bool m_bTableauTenu;

	// Événements calculés chez le joueur (sans réseau)
	protected int m_iEtatPrecedent = -1;
	protected int m_iRestantPrecedent = -1;

	//------------------------------------------------------------------------------------------------
	static SPVP_HudComponent GetInstance()
	{
		return s_Instance;
	}

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (SCR_Global.IsEditMode() || System.IsConsoleApp())
			return;

		s_Instance = this;
		GetGame().GetCallqueue().CallLater(Refresh, 100, true);

		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.AddActionListener("ShowScoreboard", EActionTrigger.DOWN, OnTableau);
		}
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		GetGame().GetCallqueue().Remove(Refresh);

		InputManager input = GetGame().GetInputManager();
		if (input)
		{
			input.RemoveActionListener("ShowScoreboard", EActionTrigger.DOWN, OnTableau);
		}

		if (m_wRoot)
			m_wRoot.RemoveFromHierarchy();
		if (s_Instance == this)
			s_Instance = null;

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	//! L'action vanilla « ShowScoreboard » envoie une seule impulsion quand le maintien est atteint :
	//! chaque impulsion ouvre ou ferme le tableau
	protected void OnTableau()
	{
		m_bTableauTenu = !m_bTableauTenu;
	}

	//------------------------------------------------------------------------------------------------
	protected bool EnsureWidgets()
	{
		if (m_wRoot)
			return true;

		WorkspaceWidget workspace = GetGame().GetWorkspace();
		if (!workspace)
			return false;

		m_wRoot = workspace.CreateWidgets(m_sLayout);
		if (!m_wRoot)
			return false;

		m_wLigne1 = TextWidget.Cast(m_wRoot.FindAnyWidget("Ligne1"));
		m_wLigne2 = TextWidget.Cast(m_wRoot.FindAnyWidget("Ligne2"));
		m_wBandeau = RichTextWidget.Cast(m_wRoot.FindAnyWidget("Bandeau"));
		m_wFil = RichTextWidget.Cast(m_wRoot.FindAnyWidget("Fil"));
		m_wPopups = RichTextWidget.Cast(m_wRoot.FindAnyWidget("Popups"));
		m_wCroix = TextWidget.Cast(m_wRoot.FindAnyWidget("Croix"));
		m_wTableau = m_wRoot.FindAnyWidget("Tableau");
		m_wTabTitre = RichTextWidget.Cast(m_wRoot.FindAnyWidget("TabTitre"));
		m_wTabPied = RichTextWidget.Cast(m_wRoot.FindAnyWidget("TabPied"));

		m_aColonnes.Clear();
		array<string> noms = {"TabRang", "TabNom", "TabKills", "TabMorts", "TabRatio", "TabPoints"};
		foreach (string nom : noms)
		{
			m_aColonnes.Insert(RichTextWidget.Cast(m_wRoot.FindAnyWidget(nom)));
		}

		if (m_wCroix)
			m_wCroix.SetVisible(false);
		if (m_wTableau)
			m_wTableau.SetVisible(false);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	protected static float Now()
	{
		return GetGame().GetWorld().GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	//! Pseudo affichable sans risque : sans < > # (balises, clés de traduction), raccourci
	static string Clean(string nom)
	{
		nom.Replace("<", "");
		nom.Replace(">", "");
		nom.Replace("#", "");
		if (nom.Length() > NOM_MAX)
			nom = nom.Substring(0, NOM_MAX - 1) + "…";
		return nom;
	}

	//------------------------------------------------------------------------------------------------
	static string Tint(string texte, string rgba)
	{
		return "<color rgba='" + rgba + "'>" + texte + "</color>";
	}

	//------------------------------------------------------------------------------------------------
	protected static void Sound(string evenement)
	{
		SCR_UISoundEntity.SoundEvent(evenement);
	}

	//================================================================================================
	// Reçu du serveur
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	//! P23 : « Jack  [M4A1]  Paul  TÊTE » ; tueur vide = mort seule
	void AddKillFeed(string tueur, string arme, string victime, int flags, int distance)
	{
		string ligne;
		if (tueur.IsEmpty())
		{
			ligne = Tint(Clean(victime), BLANC) + Tint("  est mort", GRIS);
		}
		else
		{
			string nomArme = "";
			if (!arme.IsEmpty())
				nomArme = WidgetManager.Translate(arme);

			ligne = Tint(Clean(tueur), BLANC);
			if (!nomArme.IsEmpty())
				ligne += Tint("  [" + Clean(nomArme) + "]  ", GRIS);
			else
				ligne += "  ";
			ligne += Tint(Clean(victime), BLANC);

			if (flags & SPVP_KillFlags.TETE)
				ligne += Tint("  TÊTE", ORANGE);
			if (flags & SPVP_KillFlags.ACHEVE)
				ligne += Tint("  (achevé)", GRIS);
			if (flags & SPVP_KillFlags.FUI)
				ligne += Tint("  a fui", GRIS);
		}

		SPVP_HudLine entree = new SPVP_HudLine();
		entree.m_sTexte = ligne;
		entree.m_fFin = Now() + FIL_DUREE_MS;
		m_aFil.Insert(entree);
		while (m_aFil.Count() > FIL_LIGNES)
		{
			m_aFil.RemoveOrdered(0);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! P24 : lignes de points d'un kill ou d'une assistance
	void AddPoints(notnull array<int> types, notnull array<int> valeurs, string victime)
	{
		float fin = Now() + POPUP_DUREE_MS;
		bool kill = types.Find(SPVP_ELigne.KILL) >= 0;
		if (kill)
			AddPopup(Tint(Clean(victime) + " éliminé", BLANC), fin);

		for (int i = 0; i < types.Count(); i++)
		{
			int valeur = 0;
			if (i < valeurs.Count())
				valeur = valeurs[i];

			string texte = "";
			switch (types[i])
			{
				case SPVP_ELigne.KILL: texte = string.Format("+%1 Élimination", valeur); break;
				case SPVP_ELigne.TETE: texte = string.Format("+%1 Headshot", valeur); break;
				case SPVP_ELigne.DISTANCE: texte = string.Format("+%1 Longue distance (%2 m)", valeur, FindValue(types, valeurs, SPVP_ELigne.DISTANCE_M)); break;
				case SPVP_ELigne.PRIME: texte = string.Format("+%1 Prime : leader abattu", valeur); break;
				case SPVP_ELigne.ASSIST: texte = string.Format("+%1 Assistance (%2)", valeur, Clean(victime)); break;
				case SPVP_ELigne.SERIE: texte = string.Format("Série de %1 : soin + munitions", valeur); break;
				case SPVP_ELigne.ACHEVE: texte = "(achevé)"; break;
				case SPVP_ELigne.FUI: texte = "(il a fui)"; break;
			}

			if (!texte.IsEmpty())
				AddPopup(Tint(texte, ORANGE), fin);
		}

		Sound(SCR_SoundEvent.POINTS_ADDED);
	}

	//------------------------------------------------------------------------------------------------
	protected static int FindValue(array<int> types, array<int> valeurs, int type)
	{
		int index = types.Find(type);
		if (index < 0 || index >= valeurs.Count())
			return 0;
		return valeurs[index];
	}

	//------------------------------------------------------------------------------------------------
	protected void AddPopup(string texte, float fin)
	{
		SPVP_HudLine entree = new SPVP_HudLine();
		entree.m_sTexte = texte;
		entree.m_fFin = fin;
		m_aPopups.Insert(entree);
		while (m_aPopups.Count() > 8)
		{
			m_aPopups.RemoveOrdered(0);
		}

		// Toute la pile reste tant que des points arrivent
		foreach (SPVP_HudLine autre : m_aPopups)
		{
			autre.m_fFin = fin;
		}
	}

	//------------------------------------------------------------------------------------------------
	//! C25 : croix blanche à la touche, rouge au kill
	void ShowHitMarker(bool kill)
	{
		if (!EnsureWidgets() || !m_wCroix)
			return;

		if (kill)
		{
			m_wCroix.SetColor(Color.FromRGBA(230, 40, 40, 255));
			m_bCroixRouge = true;
			m_fCroixFin = Now() + CROIX_KILL_MS;
			Sound(SCR_SoundEvent.ITEM_CONFIRMED);
		}
		else
		{
			// Une touche n'efface pas une croix rouge encore affichée
			if (m_bCroixRouge && m_fCroixFin > Now())
				return;
			m_bCroixRouge = false;
			m_wCroix.SetColor(Color.FromRGBA(255, 255, 255, 255));
			m_fCroixFin = Now() + CROIX_TOUCHE_MS;
			Sound(SCR_SoundEvent.SOUND_FE_ITEM_CHANGE);
		}
		m_wCroix.SetVisible(true);
	}

	//------------------------------------------------------------------------------------------------
	//! P25 : bandeau décidé par le serveur
	void ShowServerBanner(int type, string nom, int valeur)
	{
		switch (type)
		{
			case SPVP_EBandeau.SERIE:
			{
				ShowBanner(Tint(Clean(nom), BLANC) + Tint(string.Format(" est en série de %1 !", valeur), ORANGE));
				break;
			}

			case SPVP_EBandeau.LEADER:
			{
				string texte = Tint(Clean(nom), BLANC) + Tint(string.Format(" prend la tête (%1 kills)", valeur), ORANGE);
				if (IsLocalName(nom))
					texte = Tint(string.Format("Tu prends la tête ! (%1 kills)", valeur), ORANGE);
				ShowBanner(texte);
				break;
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected bool IsLocalName(string nom)
	{
		int localId = SCR_PlayerController.GetLocalPlayerId();
		return localId > 0 && GetGame().GetPlayerManager().GetPlayerName(localId) == nom;
	}

	//------------------------------------------------------------------------------------------------
	//! Un bandeau encore affiché n'est pas écrasé : le suivant attend son tour (au plus 3).
	//! « remplacer » sert au compte à rebours, qui doit tomber à la seconde.
	protected void ShowBanner(string texte, string son = "SOUND_HUD_NOTIFICATION", bool remplacer = false)
	{
		if (remplacer)
		{
			m_aBandeauxEnAttente.Clear();
			m_aSonsEnAttente.Clear();
		}
		else if (m_fBandeauFin > Now())
		{
			if (m_aBandeauxEnAttente.Count() < 3)
			{
				m_aBandeauxEnAttente.Insert(texte);
				m_aSonsEnAttente.Insert(son);
			}
			return;
		}

		m_sBandeau = texte;
		m_fBandeauFin = Now() + BANDEAU_DUREE_MS;
		if (!son.IsEmpty())
			Sound(son);
	}

	//================================================================================================
	// Rafraîchissement (10 fois par seconde)
	//================================================================================================

	//------------------------------------------------------------------------------------------------
	protected void Refresh()
	{
		SPVP_RoundManagerComponent rounds = SPVP_RoundManagerComponent.GetInstance();
		int localId = SCR_PlayerController.GetLocalPlayerId();
		if (!rounds || localId <= 0)
			return;

		if (!EnsureWidgets() || !m_wLigne1 || !m_wLigne2)
			return;

		// Caché quand un menu est ouvert (arsenal, carte, pause, écran de mort…)
		MenuManager menus = GetGame().GetMenuManager();
		bool menuOuvert = menus != null && (menus.IsAnyMenuOpen() || menus.IsAnyDialogOpen());
		m_wRoot.SetVisible(!menuOuvert);

		// Un menu qui s'ouvre referme le tableau (le relâchement de la touche peut s'y perdre)
		if (menuOuvert)
			m_bTableauTenu = false;

		WatchRound(rounds);
		RefreshTop(rounds, localId);
		RefreshTimed();
		RefreshTableau(rounds, localId);
	}

	//------------------------------------------------------------------------------------------------
	//! DN04 et P25 : 3-2-1-GO et « 1 minute restante », calculés chez le joueur
	protected void WatchRound(SPVP_RoundManagerComponent rounds)
	{
		int etat = rounds.GetEtat();
		int restant = rounds.GetRestant();

		if (etat == SPVP_EEtat.DEPART && restant != m_iRestantPrecedent && restant > 0 && restant <= 3)
			ShowBanner(Tint(restant.ToString(), ORANGE), SCR_SoundEvent.SOUND_RESPAWN_COUNTDOWN, true);

		if (etat == SPVP_EEtat.EN_COURS && m_iEtatPrecedent != -1 && m_iEtatPrecedent != SPVP_EEtat.EN_COURS)
			ShowBanner(Tint("GO !", ORANGE), SCR_SoundEvent.SOUND_RESPAWN_COUNTDOWN_END, true);

		if (etat == SPVP_EEtat.EN_COURS && m_iRestantPrecedent > 60 && restant <= 60 && restant > 0)
			ShowBanner(Tint("1 minute restante", ORANGE));

		if (etat == SPVP_EEtat.FIN && m_iEtatPrecedent == SPVP_EEtat.EN_COURS)
			ShowBanner(Tint("Fin de la manche", ORANGE), "SOUND_HUD_NOTIFICATION", true);

		m_iEtatPrecedent = etat;
		m_iRestantPrecedent = restant;
	}

	//------------------------------------------------------------------------------------------------
	protected void RefreshTop(SPVP_RoundManagerComponent rounds, int localId)
	{
		int kills, morts, rang, lieu;
		bool classe = rounds.GetScore(localId, kills, morts, rang, lieu);

		string ligne1;
		switch (rounds.GetEtat())
		{
			case SPVP_EEtat.ATTENTE:
			{
				ligne1 = "En attente de joueurs";
				if (!rounds.IsAutorise())
					ligne1 = "Serveur non autorisé";
				break;
			}

			case SPVP_EEtat.EN_COURS:
			{
				ligne1 = SPVP_RoundManagerComponent.FormatTemps(rounds.GetRestant()) + "   ·   " + rounds.GetVille();
				if (rounds.IsNuit())
					ligne1 += " (nuit)";
				break;
			}

			case SPVP_EEtat.FIN:
			{
				ligne1 = "Fin de manche   ·   résultats";
				break;
			}

			case SPVP_EEtat.VOTE:
			{
				ligne1 = "Vote (" + rounds.GetRestant().ToString() + " s) : ";
				for (int i = 0; i < rounds.GetVoteCount(); i++)
				{
					if (i > 0)
						ligne1 += "   ·   ";
					ligne1 += string.Format("%1 %2", rounds.GetVoteNom(i), rounds.GetVoteVoix(i));
				}
				break;
			}

			case SPVP_EEtat.DEPART:
			{
				ligne1 = string.Format("%1   ·   DÉPART DANS %2", rounds.GetVille(), rounds.GetRestant());
				break;
			}
		}

		if (classe && lieu == SPVP_ELieu.LOBBY)
			ligne1 = "ZONE SÛRE   ·   " + ligne1;
		else if (classe && lieu == SPVP_RoundManagerComponent.LIEU_FILE)
			ligne1 = "EN FILE POUR LE DÉPART   ·   " + ligne1;

		m_wLigne1.SetText(ligne1);

		string ligne2 = "";
		if (classe)
		{
			int points = rounds.GetPoints(rang - 1);
			ligne2 = string.Format("%1 kills   ·   %2 pts   ·   %3e/%4", kills, points, rang, rounds.GetClassementCount());

			int premierId, premierKills, premierMorts;
			if (rounds.GetClassementCount() > 0)
			{
				rounds.GetLigne(0, premierId, premierKills, premierMorts);
				if (premierId == localId)
					ligne2 += "   ·   1er : toi";
				else
					ligne2 += string.Format("   ·   1er : %1 (%2)", Clean(rounds.GetNom(0)), premierKills);
			}
		}
		m_wLigne2.SetText(ligne2);
	}

	//------------------------------------------------------------------------------------------------
	//! Fil, popups, bandeau et croix : on retire ce qui a expiré
	protected void RefreshTimed()
	{
		float maintenant = Now();

		for (int i = m_aFil.Count() - 1; i >= 0; i--)
		{
			if (m_aFil[i].m_fFin <= maintenant)
				m_aFil.RemoveOrdered(i);
		}
		for (int j = m_aPopups.Count() - 1; j >= 0; j--)
		{
			if (m_aPopups[j].m_fFin <= maintenant)
				m_aPopups.RemoveOrdered(j);
		}

		if (m_wFil)
			m_wFil.SetText(Join(m_aFil));
		if (m_wPopups)
			m_wPopups.SetText(Join(m_aPopups));

		if (m_wBandeau)
		{
			if (m_fBandeauFin <= maintenant && !m_aBandeauxEnAttente.IsEmpty())
			{
				string suivant = m_aBandeauxEnAttente[0];
				string son = m_aSonsEnAttente[0];
				m_aBandeauxEnAttente.RemoveOrdered(0);
				m_aSonsEnAttente.RemoveOrdered(0);
				ShowBanner(suivant, son);
			}

			if (m_fBandeauFin > maintenant)
				m_wBandeau.SetText(m_sBandeau);
			else
				m_wBandeau.SetText("");
		}

		if (m_wCroix && m_fCroixFin <= maintenant)
			m_wCroix.SetVisible(false);
	}

	//------------------------------------------------------------------------------------------------
	protected static string Join(array<ref SPVP_HudLine> lignes)
	{
		string texte = "";
		foreach (SPVP_HudLine ligne : lignes)
		{
			if (!texte.IsEmpty())
				texte += "\n";
			texte += ligne.m_sTexte;
		}
		return texte;
	}

	//------------------------------------------------------------------------------------------------
	//! P22 : tableau tant qu'on tient la touche ; P20 : résultats affichés d'office en fin de manche
	protected void RefreshTableau(SPVP_RoundManagerComponent rounds, int localId)
	{
		if (!m_wTableau)
			return;

		bool resultats = rounds.GetEtat() == SPVP_EEtat.FIN && !rounds.GetResultats().IsEmpty();
		bool visible = m_bTableauTenu || resultats;
		m_wTableau.SetVisible(visible);
		if (!visible)
			return;

		// Titre
		string titre;
		if (resultats)
			titre = ResultsText(rounds.GetResultats());
		else
			titre = Tint("TABLEAU DES SCORES", ORANGE) + Tint("   ·   " + rounds.GetVille(), BLANC);
		if (m_wTabTitre)
			m_wTabTitre.SetText(titre);

		// Colonnes (même nombre de lignes partout pour qu'elles restent alignées)
		array<string> cols = {"", "", "", "", "", ""};
		cols[0] = Tint("#", GRIS);
		cols[1] = Tint("Nom", GRIS);
		cols[2] = Tint("Kills", GRIS);
		cols[3] = Tint("Morts", GRIS);
		cols[4] = Tint("Ratio", GRIS);
		cols[5] = Tint("Points", GRIS);

		int total = rounds.GetClassementCount();
		int monIndex = -1;
		for (int i = 0; i < total; i++)
		{
			int id, kills, morts;
			rounds.GetLigne(i, id, kills, morts);
			if (id == localId)
				monIndex = i;
		}

		for (int r = 0; r < total; r++)
		{
			// Au-delà de la limite : seulement ta ligne
			if (r >= TABLEAU_LIGNES && r != monIndex)
				continue;
			AddRow(rounds, r, localId, cols);
		}

		for (int c = 0; c < m_aColonnes.Count(); c++)
		{
			if (m_aColonnes[c])
				m_aColonnes[c].SetText(cols[c]);
		}

		if (m_wTabPied)
		{
			string pied = string.Format("Manche %1   ·   %2", rounds.GetManche(), rounds.GetVille());
			if (rounds.GetEtat() == SPVP_EEtat.EN_COURS)
				pied += "   ·   " + SPVP_RoundManagerComponent.FormatTemps(rounds.GetRestant()) + " restantes";
			pied += string.Format("   ·   %1 combattants", total);
			m_wTabPied.SetText(Tint(pied, GRIS));
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AddRow(SPVP_RoundManagerComponent rounds, int index, int localId, notnull array<string> cols)
	{
		int id, kills, morts;
		rounds.GetLigne(index, id, kills, morts);

		string couleur = BLANC;
		if (id == localId)
			couleur = ORANGE;
		else if (id < 0)
			couleur = GRIS;

		string nom = Clean(rounds.GetNom(index));
		int lieu = rounds.GetLieu(index);
		if (lieu == SPVP_ELieu.LOBBY || lieu == SPVP_RoundManagerComponent.LIEU_FILE)
			nom += "  (lobby)";

		int diviseur = morts;
		if (diviseur < 1)
			diviseur = 1;
		int centiemes = Math.Round(kills * 100.0 / diviseur);
		string decimales = (centiemes % 100).ToString();
		if (centiemes % 100 < 10)
			decimales = "0" + decimales;
		string ratio = (centiemes / 100).ToString() + "," + decimales;

		cols[0] = cols[0] + "\n" + Tint((index + 1).ToString(), couleur);
		cols[1] = cols[1] + "\n" + Tint(nom, couleur);
		cols[2] = cols[2] + "\n" + Tint(kills.ToString(), couleur);
		cols[3] = cols[3] + "\n" + Tint(morts.ToString(), couleur);
		cols[4] = cols[4] + "\n" + Tint(ratio, couleur);
		cols[5] = cols[5] + "\n" + Tint(rounds.GetPoints(index).ToString(), couleur);
	}

	//------------------------------------------------------------------------------------------------
	//! P20 : vainqueur, podium, médailles
	protected string ResultsText(string resultats)
	{
		string vainqueur = Tint("FIN DE LA MANCHE   ·   Pas de vainqueur", ORANGE);
		string podium = "";
		string medailles = "";

		array<string> lignes = {};
		resultats.Split("\n", lignes, true);
		foreach (string ligne : lignes)
		{
			array<string> champs = {};
			ligne.Split("|", champs, false);
			if (champs.IsEmpty())
				continue;

			if (champs[0] == "V" && champs.Count() >= 5)
			{
				vainqueur = Tint("VAINQUEUR : ", ORANGE) + Tint(Clean(champs[1]), BLANC) + Tint(string.Format("   %1 kills · %2 morts · %3 pts", champs[2], champs[3], champs[4]), GRIS);
			}
			else if (champs[0] == "P" && champs.Count() >= 6)
			{
				if (!podium.IsEmpty())
					podium += "      ";
				podium += Tint(champs[1] + ". ", ORANGE) + Tint(Clean(champs[2]), BLANC) + Tint(string.Format(" (%1 kills)", champs[3]), GRIS);
			}
			else if (champs[0] == "M" && champs.Count() >= 4)
			{
				if (!medailles.IsEmpty())
					medailles += "\n";
				medailles += MedalText(champs[1], Clean(champs[2]), champs[3]);
			}
		}

		string texte = vainqueur;
		if (!podium.IsEmpty())
			texte += "\n" + podium;
		if (!medailles.IsEmpty())
			texte += "\n" + medailles;
		return texte;
	}

	//------------------------------------------------------------------------------------------------
	protected string MedalText(string code, string nom, string valeur)
	{
		string titre = code;
		string unite = "";
		switch (code)
		{
			case "tir": titre = "Plus long tir"; unite = " m"; break;
			case "tetes": titre = "Plus de headshots"; break;
			case "serie": titre = "Plus longue série"; unite = " kills"; break;
			case "morts": titre = "Plus de morts"; break;
		}
		return Tint(titre + " : ", GRIS) + Tint(nom, BLANC) + Tint(" (" + valeur + unite + ")", GRIS);
	}
}

//------------------------------------------------------------------------------------------------
//! P22 : la touche du tableau (P maintenu / View + Menu) ouvre notre tableau, plus la liste vanilla.
//! La liste vanilla (couper un micro, bloquer) reste dans le menu Échap, bouton « Joueurs ».
modded class ArmaReforgerScripted
{
	//------------------------------------------------------------------------------------------------
	override void AddActionListeners()
	{
		super.AddActionListeners();
		GetInputManager().RemoveActionListener("ShowScoreboard", EActionTrigger.DOWN, OnShowPlayerList);
	}
}
