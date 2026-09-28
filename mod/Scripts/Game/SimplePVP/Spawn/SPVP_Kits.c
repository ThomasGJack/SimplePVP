//------------------------------------------------------------------------------------------------
// SimplePVP — Kits tout prêts (A08, A09) et noms d'équipement
// Au démarrage du serveur, chaque soldat vanilla de la liste (Assaut, Mitrailleur…) est créé un instant sous le
// lobby, son équipement est lu comme un loadout, puis il est supprimé. Les joueurs prennent un kit à l'ordinateur
// du lobby ; celui qui part sans avoir rien choisi et sans arme à feu reçoit un kit au hasard (A09).
// Livraison 3
//------------------------------------------------------------------------------------------------

class SPVP_Kits
{
	protected static ref array<string> s_aNoms;
	protected static ref array<string> s_aKits;

	//------------------------------------------------------------------------------------------------
	static void Reset()
	{
		s_aNoms = {};
		s_aKits = {};
	}

	//------------------------------------------------------------------------------------------------
	//! Serveur, au démarrage : lit l'équipement de chaque soldat de la liste
	static void Prepare(notnull array<ResourceName> soldats, notnull array<string> noms)
	{
		vector position;
		float cap;
		SPVP_Lobby.GetArrivee(position, cap);
		position[1] = position[1] - 40;	// sous le lobby, invisible

		// Une place par kit dès le départ : un kit raté reste vide, les numéros des autres ne bougent pas
		for (int j = 0; j < soldats.Count(); j++)
		{
			s_aNoms.Insert("");
			s_aKits.Insert("");
		}

		for (int i = 0; i < soldats.Count(); i++)
		{
			Resource res = Resource.Load(soldats[i]);
			if (!res || !res.IsValid())
			{
				SPVP_Log.Error("Kit prêt : prefab invalide " + soldats[i]);
				continue;
			}

			EntitySpawnParams params = new EntitySpawnParams();
			params.TransformMode = ETransformMode.WORLD;
			params.Transform[3] = position + Vector(i * 3, 0, 0);
			IEntity soldat = GetGame().SpawnEntityPrefab(res, GetGame().GetWorld(), params);
			if (!soldat)
				continue;

			string nom = "Kit " + (i + 1).ToString();
			if (i < noms.Count())
				nom = noms[i];

			// L'inventaire se remplit après l'apparition : lecture une seconde plus tard
			GetGame().GetCallqueue().CallLater(Capture, 1500, false, soldat, nom, i);
		}
	}

	//------------------------------------------------------------------------------------------------
	protected static void Capture(IEntity soldat, string nom, int index)
	{
		if (!soldat)
			return;

		string kit = SPVP_Kit.Capture(soldat);
		if (!kit.IsEmpty() && index >= 0 && index < s_aKits.Count())
		{
			s_aNoms[index] = nom;
			s_aKits[index] = kit;
			SPVP_Log.Info("Kit prêt : " + nom + " (" + SPVP_Kit.Label(soldat) + ")");
		}
		else
		{
			SPVP_Log.Warn("Kit prêt illisible : " + nom);
		}

		SCR_EntityHelper.DeleteEntityAndChildren(soldat);
	}

	//------------------------------------------------------------------------------------------------
	static int Count()
	{
		if (!s_aKits)
			return 0;
		return s_aKits.Count();
	}

	//------------------------------------------------------------------------------------------------
	static string GetNom(int index)
	{
		if (!s_aNoms || index < 0 || index >= s_aNoms.Count())
			return "";
		return s_aNoms[index];
	}

	//------------------------------------------------------------------------------------------------
	static string GetKit(int index)
	{
		if (!s_aKits || index < 0 || index >= s_aKits.Count())
			return "";
		return s_aKits[index];
	}

	//------------------------------------------------------------------------------------------------
	static int GetRandomIndex()
	{
		array<int> prets = {};
		for (int i = 0; i < Count(); i++)
		{
			if (!GetKit(i).IsEmpty())
				prets.Insert(i);
		}

		if (prets.IsEmpty())
			return -1;
		return prets.GetRandomElement();
	}
}

