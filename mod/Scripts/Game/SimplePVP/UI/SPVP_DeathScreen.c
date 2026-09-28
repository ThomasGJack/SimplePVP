//------------------------------------------------------------------------------------------------
// SimplePVP — Écran de mort (chez le joueur)
// - D16 : qui t'a tué, avec quoi, à quelle distance, et la vie qui lui reste.
// - D17 : la caméra monte au-dessus du corps et regarde d'où venait le tir (jamais la position du tueur : D18).
// - D01, D02, A07 : choix de l'équipement pour repartir, ou retour au lobby ; réapparition 5 s après la mort (D10).
// Le menu se rouvre tout seul s'il est fermé tant que le joueur n'a pas choisi. Sans choix au bout de 90 s,
// le serveur le renvoie au lobby.
// Livraison 3
//------------------------------------------------------------------------------------------------

class SPVP_DeathCameraClass : CameraBaseClass
{
}

class SPVP_DeathCamera : CameraBase
{
	//------------------------------------------------------------------------------------------------
	//! Fixe : au-dessus du corps, un peu en arrière, regard vers la direction du tir
	void Setup(vector corps, vector direction)
	{
		if (direction.Length() < 0.1)
			direction = Vector(0, 0, 1);

		vector position = corps - direction * 4 + Vector(0, 6, 0);
		float sol = GetGame().GetWorld().GetSurfaceY(position[0], position[2]);
		if (position[1] < sol + 1)
			position[1] = sol + 1;

		vector cible = corps + direction * 20 + Vector(0, 1, 0);
		vector regard = (cible - position).Normalized();

		vector mat[4];
		Math3D.DirectionAndUpMatrix(regard, vector.Up, mat);
		mat[3] = position;
		SetTransform(mat);
	}
}

//------------------------------------------------------------------------------------------------
class SPVP_DeathScreen
{
	protected static bool s_bActif;
	protected static bool s_bChoisi;			// choix envoyé : on attend la réapparition, le menu ne revient plus
	protected static string s_sContenu;
	protected static float s_fReapparition;		// heure du monde (ms) où la réapparition devient possible
	protected static SPVP_DeathCamera s_Camera;
	protected static CameraBase s_CameraAvant;

	//------------------------------------------------------------------------------------------------
	//! Le serveur annonce la mort : caméra et menu
	static void Start(string contenu, vector corps, vector direction, int delai)
	{
		Stop();
		DeleteCamera();
		s_bActif = true;
		s_bChoisi = false;
		s_sContenu = contenu;
		s_fReapparition = GetGame().GetWorld().GetWorldTime() + delai * 1000;

		StartCamera(corps, direction);
		OpenMenu();
		GetGame().GetCallqueue().CallLater(Update, 500, true);
	}

	//------------------------------------------------------------------------------------------------
	//! Réapparu (ou renvoyé au lobby) : tout se ferme
	static void Stop()
	{
		GetGame().GetCallqueue().Remove(RestoreCamera);
		GetGame().GetCallqueue().Remove(Update);
		s_bActif = false;

		SPVP_ChoiceMenu menu = FindMenu();
		if (menu)
			menu.Close();

		RestoreCamera(0);
	}

	//------------------------------------------------------------------------------------------------
	//! Le joueur a validé son choix : le menu ne se rouvre plus jusqu'à la réapparition
	static void OnChoiceSent()
	{
		s_bChoisi = true;
	}

	//------------------------------------------------------------------------------------------------
	protected static SPVP_ChoiceMenu FindMenu()
	{
		MenuManager menus = GetGame().GetMenuManager();
		if (!menus)
			return null;

		SPVP_ChoiceMenu menu = SPVP_ChoiceMenu.Cast(menus.FindMenuByPreset(ChimeraMenuPreset.SPVP_ChoiceMenu));
		if (menu && menu.GetKind() == "mort")
			return menu;
		return null;
	}

	//------------------------------------------------------------------------------------------------
	protected static void OpenMenu()
	{
		SPVP_ChoiceMenu.Open("mort", "ÉLIMINÉ", "Réapparaître", "Fermer", s_sContenu);
	}

	//------------------------------------------------------------------------------------------------
	//! Deux fois par seconde : compte à rebours, menu rouvert s'il a été fermé
	protected static void Update()
	{
		if (!s_bActif || s_bChoisi)
			return;

		SPVP_ChoiceMenu menu = FindMenu();
		if (!menu)
		{
			// Fermé sans choix : on le rouvre, sauf si un autre écran est ouvert (pause, carte…)
			if (!GetGame().GetMenuManager().GetTopMenu())
				OpenMenu();
			return;
		}

		float reste = (s_fReapparition - GetGame().GetWorld().GetWorldTime()) / 1000;
		string texte = GetMessage();
		if (reste > 0)
			texte += string.Format("\nRéapparition possible dans %1 s. Choisis ton équipement.", Math.Ceil(reste));
		else
			texte += "\nChoisis ton équipement et réapparais.";
		menu.SetInfo(texte);
	}

	//------------------------------------------------------------------------------------------------
	protected static string GetMessage()
	{
		array<string> lignes = {};
		s_sContenu.Split("\n", lignes, true);
		foreach (string ligne : lignes)
		{
			if (ligne.StartsWith("M|"))
				return ligne.Substring(2, ligne.Length() - 2);
		}
		return "";
	}

	//------------------------------------------------------------------------------------------------
	protected static void StartCamera(vector corps, vector direction)
	{
		CameraManager cameras = GetGame().GetCameraManager();
		if (!cameras)
			return;

		s_CameraAvant = cameras.CurrentCamera();

		EntitySpawnParams params = new EntitySpawnParams();
		params.TransformMode = ETransformMode.WORLD;
		params.Transform[3] = corps;
		s_Camera = SPVP_DeathCamera.Cast(GetGame().SpawnEntity(SPVP_DeathCamera, GetGame().GetWorld(), params));
		if (!s_Camera)
			return;

		s_Camera.Setup(corps, direction);
		cameras.SetCamera(s_Camera);
	}

	//------------------------------------------------------------------------------------------------
	//! Rend la main à la caméra du personnage ; celle du nouveau personnage peut arriver un peu après : jusqu'à 10 essais
	protected static void RestoreCamera(int essai)
	{
		if (!s_Camera)
			return;

		CameraManager cameras = GetGame().GetCameraManager();
		if (!cameras || cameras.CurrentCamera() != s_Camera)
		{
			DeleteCamera();
			return;
		}

		CameraBase cible = s_CameraAvant;
		if (!cible || cible == s_Camera)
		{
			array<CameraBase> liste = {};
			cameras.GetCamerasList(liste);
			foreach (CameraBase camera : liste)
			{
				if (camera && camera != s_Camera)
				{
					cible = camera;
					break;
				}
			}
		}

		if (cible && cible != s_Camera)
		{
			cameras.SetCamera(cible);
			DeleteCamera();
			return;
		}

		if (essai < 10)
			GetGame().GetCallqueue().CallLater(RestoreCamera, 200, false, essai + 1);
		else
			DeleteCamera();
	}

	//------------------------------------------------------------------------------------------------
	protected static void DeleteCamera()
	{
		if (s_Camera)
			SCR_EntityHelper.DeleteEntityAndChildren(s_Camera);
		s_Camera = null;
		s_CameraAvant = null;
	}
}

//------------------------------------------------------------------------------------------------
//! L'écran ne doit pas noircir tout seul à la mort : la caméra et l'écran de mort doivent rester visibles
modded class SCR_DeathScreenEffect
{
	//------------------------------------------------------------------------------------------------
	override protected void DeathEffect()
	{
	}

	//------------------------------------------------------------------------------------------------
	override protected void InstaDeathEffect()
	{
	}
}
