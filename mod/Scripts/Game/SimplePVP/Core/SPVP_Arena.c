//------------------------------------------------------------------------------------------------
// SimplePVP — Lieu de combat (ville, quartier, site militaire…) : un cercle au centre du lieu
// Deux tailles de zone : petite sous le seuil de joueurs, grande au-dessus (V09).
// Les quartiers d'une même grande ville partagent un « groupe » : jamais deux de suite, jamais deux au vote (V07).
// Minimum et maximum de joueurs : le lieu n'est proposé que dans cette fourchette (V18).
// Livraison 2
//------------------------------------------------------------------------------------------------

class SPVP_Arena
{
	string m_sNom;
	float m_fX;
	float m_fZ;
	float m_fRayonPetit = 100;
	float m_fRayonGrand = 150;
	string m_sGroupe;					// vide = son propre nom
	int m_iJoueursMin = 0;
	int m_iJoueursMax = 99;
	bool m_bActif = true;

	//------------------------------------------------------------------------------------------------
	void SPVP_Arena(string nom = "", float x = 0, float z = 0)
	{
		m_sNom = nom;
		m_fX = x;
		m_fZ = z;
	}

	//------------------------------------------------------------------------------------------------
	//! Centre posé sur le sol
	vector GetCenter()
	{
		vector center = Vector(m_fX, 0, m_fZ);
		center[1] = SCR_TerrainHelper.GetTerrainY(center);
		return center;
	}

	//------------------------------------------------------------------------------------------------
	string GetGroupe()
	{
		if (m_sGroupe.IsEmpty())
			return m_sNom;
		return m_sGroupe;
	}

	//------------------------------------------------------------------------------------------------
	bool Accepte(int joueurs)
	{
		return m_bActif && joueurs >= m_iJoueursMin && joueurs <= m_iJoueursMax;
	}
}

//------------------------------------------------------------------------------------------------
// SimplePVP — Journal (préfixe [SPVP] dans le log du serveur)
//------------------------------------------------------------------------------------------------
class SPVP_Log
{
	//------------------------------------------------------------------------------------------------
	static void Info(string text)
	{
		Print("[SPVP] " + text, LogLevel.NORMAL);
	}

	//------------------------------------------------------------------------------------------------
	static void Warn(string text)
	{
		Print("[SPVP] " + text, LogLevel.WARNING);
	}

	//------------------------------------------------------------------------------------------------
	static void Error(string text)
	{
		Print("[SPVP] " + text, LogLevel.ERROR);
	}
}
