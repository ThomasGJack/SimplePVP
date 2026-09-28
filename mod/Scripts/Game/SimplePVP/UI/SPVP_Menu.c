//------------------------------------------------------------------------------------------------
// SimplePVP — Menu à liste (écran de dialogue vanilla + layout maison), jouable à la manette
// Contenu envoyé par le serveur, une ligne par entrée :
//   M|message    H|titre de section    L|ligne simple    I|clé|libellé (ligne à choisir)
// Souris : clic sur une ligne pour la choisir, puis Valider. Manette : la ligne qui a le focus (croix
// directionnelle) est la ligne choisie, A valide (action vanilla DialogConfirm), B ferme.
// Mécanique reprise de ce qui marche sur la PAG (écrite à neuf pour SimplePVP).
// Livraison 3
//------------------------------------------------------------------------------------------------

modded enum ChimeraMenuPreset
{
	SPVP_ChoiceMenu
};

//------------------------------------------------------------------------------------------------
class SPVP_MenuEntry
{
	string m_sKey;
	string m_sLabel;
	bool m_bSelectable;
	Widget m_wRoot;
	TextWidget m_wLabel;
}

//------------------------------------------------------------------------------------------------
class SPVP_MenuEntryHandler : ScriptedWidgetEventHandler
{
	protected SPVP_ChoiceMenu m_Menu;
	protected int m_iIndex;

	//------------------------------------------------------------------------------------------------
	void SPVP_MenuEntryHandler(SPVP_ChoiceMenu menu, int index)
	{
		m_Menu = menu;
		m_iIndex = index;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnClick(Widget w, int x, int y, int button)
	{
		if (m_Menu)
			m_Menu.Highlight(m_iIndex);
		return true;
	}

	//------------------------------------------------------------------------------------------------
	override bool OnFocus(Widget w, int x, int y)
	{
		if (m_Menu && SPVP_ChoiceMenu.IsGamepad())
			m_Menu.Highlight(m_iIndex);
		return false;
	}
}

//------------------------------------------------------------------------------------------------
class SPVP_ChoiceMenu : DialogUI
{
	static const ResourceName ENTRY_LAYOUT = "{6A7D3B9C1E5F2A04}UI/layouts/SimplePVP/SPVP_Entry.layout";

	protected Widget m_wList;
	protected ref array<ref SPVP_MenuEntry> m_aEntries = {};
	protected ref array<ref SPVP_MenuEntryHandler> m_aHandlers = {};
	protected int m_iSelected = -1;
	protected string m_sKind;			// ce que le serveur doit faire du choix (ex. « mort »)
	protected string m_sMessage;

	//------------------------------------------------------------------------------------------------
	static bool IsGamepad()
	{
		InputManager input = GetGame().GetInputManager();
		return input && input.GetLastUsedInputDevice() == EInputDeviceType.GAMEPAD;
	}

	//------------------------------------------------------------------------------------------------
	//! Ouvre le menu (chez le joueur)
	static SPVP_ChoiceMenu Open(string kind, string titre, string confirmer, string fermer, string contenu)
	{
		SPVP_ChoiceMenu menu = SPVP_ChoiceMenu.Cast(GetGame().GetMenuManager().OpenDialog(ChimeraMenuPreset.SPVP_ChoiceMenu));
		if (!menu)
		{
			SPVP_Log.Error("Menu SimplePVP introuvable : preset SPVP_ChoiceMenu absent de chimeraMenus.conf ?");
			return null;
		}

		menu.m_sKind = kind;
		menu.SetTitle(titre);
		menu.SetConfirmText(confirmer);
		menu.SetCancelText(fermer);
		menu.Fill(contenu);
		return menu;
	}

	//------------------------------------------------------------------------------------------------
	string GetKind()
	{
		return m_sKind;
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuOpen()
	{
		super.OnMenuOpen();
		m_wList = GetRootWidget().FindAnyWidget("List");
	}

	//------------------------------------------------------------------------------------------------
	override void OnMenuClose()
	{
		GetGame().GetCallqueue().Remove(FocusFirst);
		super.OnMenuClose();
	}

	//------------------------------------------------------------------------------------------------
	void Fill(string contenu)
	{
		foreach (SPVP_MenuEntry old : m_aEntries)
		{
			if (old.m_wRoot)
				old.m_wRoot.RemoveFromHierarchy();
		}
		m_aEntries.Clear();
		m_aHandlers.Clear();
		m_iSelected = -1;

		array<string> lignes = {};
		contenu.Split("\n", lignes, true);
		foreach (string ligne : lignes)
		{
			array<string> champs = {};
			ligne.Split("|", champs, false);
			if (champs.Count() < 2)
				continue;

			if (champs[0] == "M")
			{
				m_sMessage = champs[1];
				SetMessage(m_sMessage);
			}
			else if (champs[0] == "H")
			{
				AddEntry("", "— " + champs[1] + " —", false);
			}
			else if (champs[0] == "L")
			{
				AddEntry("", champs[1], false);
			}
			else if (champs[0] == "I" && champs.Count() >= 3)
			{
				AddEntry(champs[1], champs[2], true);
			}
		}

		// Manette : focus sur la première ligne à choisir
		GetGame().GetCallqueue().Remove(FocusFirst);
		GetGame().GetCallqueue().CallLater(FocusFirst, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Texte du haut, sans toucher aux lignes (compte à rebours de l'écran de mort)
	void SetInfo(string texte)
	{
		SetMessage(texte);
	}

	//------------------------------------------------------------------------------------------------
	protected void FocusFirst()
	{
		if (GetGame().GetMenuManager().GetTopMenu() != this)
			return;

		foreach (int i, SPVP_MenuEntry entry : m_aEntries)
		{
			if (!entry.m_bSelectable || !entry.m_wRoot)
				continue;

			if (IsGamepad())
				GetGame().GetWorkspace().SetFocusedWidget(entry.m_wRoot);
			Highlight(i);
			return;
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void AddEntry(string key, string label, bool selectable)
	{
		if (!m_wList)
			return;

		Widget root = GetGame().GetWorkspace().CreateWidgets(ENTRY_LAYOUT, m_wList);
		if (!root)
		{
			SPVP_Log.Error("Gabarit de ligne de menu introuvable : " + ENTRY_LAYOUT);
			return;
		}

		LayoutSlot.SetHorizontalAlign(root, 3);	// toute la largeur
		if (!selectable)
			root.SetFlags(WidgetFlags.NOFOCUS);

		SPVP_MenuEntry entry = new SPVP_MenuEntry();
		entry.m_sKey = key;
		entry.m_sLabel = label;
		entry.m_bSelectable = selectable;
		entry.m_wRoot = root;
		entry.m_wLabel = TextWidget.Cast(root.FindAnyWidget("Label"));
		if (entry.m_wLabel)
		{
			entry.m_wLabel.SetText(label);
			entry.m_wLabel.SetFlags(WidgetFlags.IGNORE_CURSOR);
		}

		int index = m_aEntries.Count();
		m_aEntries.Insert(entry);

		if (selectable)
		{
			SPVP_MenuEntryHandler handler = new SPVP_MenuEntryHandler(this, index);
			m_aHandlers.Insert(handler);
			root.AddHandler(handler);
		}

		Paint(entry, false);
	}

	//------------------------------------------------------------------------------------------------
	//! Fond sombre au repos, orange quand la ligne est choisie, transparent pour un titre de section
	protected void Paint(SPVP_MenuEntry entry, bool selected)
	{
		if (entry.m_wRoot)
		{
			if (!entry.m_bSelectable)
				entry.m_wRoot.SetColor(Color.FromRGBA(0, 0, 0, 0));
			else if (selected)
				entry.m_wRoot.SetColor(Color.FromRGBA(255, 106, 19, 230));
			else
				entry.m_wRoot.SetColor(Color.FromRGBA(23, 26, 31, 210));
		}

		if (entry.m_wLabel)
		{
			if (entry.m_bSelectable)
				entry.m_wLabel.SetColor(Color.FromRGBA(242, 242, 242, 255));
			else
				entry.m_wLabel.SetColor(Color.FromRGBA(154, 160, 166, 255));
		}
	}

	//------------------------------------------------------------------------------------------------
	void Highlight(int index)
	{
		if (index < 0 || index >= m_aEntries.Count() || !m_aEntries[index].m_bSelectable)
			return;

		if (m_iSelected >= 0 && m_iSelected < m_aEntries.Count())
			Paint(m_aEntries[m_iSelected], false);

		m_iSelected = index;
		Paint(m_aEntries[index], true);
	}

	//------------------------------------------------------------------------------------------------
	//! Valider : la clé choisie part au serveur
	override protected void OnConfirm()
	{
		if (m_iSelected < 0 || m_iSelected >= m_aEntries.Count())
			return;

		SCR_PlayerController pc = SCR_PlayerController.Cast(GetGame().GetPlayerController());
		if (pc)
			pc.SPVP_SendChoice(m_sKind, m_aEntries[m_iSelected].m_sKey);

		if (m_sKind == "mort")
			SPVP_DeathScreen.OnChoiceSent();

		Close();
	}
}
