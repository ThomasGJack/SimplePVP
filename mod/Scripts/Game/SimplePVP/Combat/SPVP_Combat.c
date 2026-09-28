//------------------------------------------------------------------------------------------------
// SimplePVP — Protection d'apparition, tirs et zones dangereuses (serveur)
// - D12-D14 : 5 s après une apparition en ville, 10 % des dégâts ; la protection s'arrête dès qu'on tire ou vise.
//   Même règle pour les bots (I10).
// - D09 : un point d'apparition n'est pas utilisé pendant 30 s près (40 m) d'un tir ou d'une mort récente.
// - D08 : points d'apparition posés à la main (étages, toits) : entités SPVP_SpawnPoint dans le monde.
// Livraison 3
//------------------------------------------------------------------------------------------------

//! Écoute les tirs d'un personnage (le jeu appelle une méthode d'objet, pas une fonction statique)
class SPVP_ShotWatch : Managed
{
	IEntity m_Owner;

	//------------------------------------------------------------------------------------------------
	void SPVP_ShotWatch(IEntity owner)
	{
		m_Owner = owner;
	}

	//------------------------------------------------------------------------------------------------
	void OnShot(int playerID, BaseWeaponComponent weapon, IEntity entity)
	{
		SPVP_Combat.OnShot(m_Owner);
	}
}

//------------------------------------------------------------------------------------------------
class SPVP_Combat
{
	static const float PROTECTION_S = 5;
	static const float PROTECTION_FACTEUR = 0.1;
	static const float DANGER_RAYON = 40;
	static const float DANGER_S = 30;

	protected static ref map<IEntity, float> s_mProtection;		// personnage → fin de protection (ms du monde)
	protected static ref array<ref SPVP_ShotWatch> s_aWatches;
	protected static ref array<vector> s_aDangerPos;
	protected static ref array<float> s_aDangerTemps;
	protected static ref array<float> s_aDernierTir;				// même index que s_aWatches : dernière marque posée

	//------------------------------------------------------------------------------------------------
	static void Reset()
	{
		s_mProtection = new map<IEntity, float>();
		s_aWatches = {};
		s_aDernierTir = {};
		s_aDangerPos = {};
		s_aDangerTemps = {};
	}

	//------------------------------------------------------------------------------------------------
	protected static float Now()
	{
		return GetGame().GetWorld().GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	//! Apparition en ville (joueur ou bot) : protection et écoute de ses tirs
	static void OnSpawnedInCity(IEntity character)
	{
		if (!character || !s_mProtection)
			return;

		s_mProtection.Set(character, Now() + PROTECTION_S * 1000);

		// Nettoyage des écoutes de personnages disparus
		for (int i = s_aWatches.Count() - 1; i >= 0; i--)
		{
			IEntity owner = s_aWatches[i].m_Owner;
			if (!owner || owner.IsDeleted() || SPVP_Players.IsDead(owner))
			{
				s_aWatches.Remove(i);
				s_aDernierTir.Remove(i);
			}
		}

		EventHandlerManagerComponent events = EventHandlerManagerComponent.Cast(character.FindComponent(EventHandlerManagerComponent));
		if (!events)
			return;

		SPVP_ShotWatch watch = new SPVP_ShotWatch(character);
		s_aWatches.Insert(watch);
		s_aDernierTir.Insert(0);
		events.RegisterScriptHandler("OnProjectileShot", watch, watch.OnShot);
		events.RegisterScriptHandler("OnGrenadeThrown", watch, watch.OnShot);
	}

	//------------------------------------------------------------------------------------------------
	//! Un personnage a tiré : fin de sa protection, zone dangereuse (au plus une marque toutes les 2 s par tireur)
	static void OnShot(IEntity shooter)
	{
		if (!shooter || !s_mProtection)
			return;

		s_mProtection.Remove(shooter);

		float now = Now();
		for (int i = 0; i < s_aWatches.Count(); i++)
		{
			if (s_aWatches[i].m_Owner != shooter)
				continue;
			if (now - s_aDernierTir[i] < 2000)
				return;
			s_aDernierTir[i] = now;
			break;
		}
		AddDanger(shooter.GetOrigin());
	}

	//------------------------------------------------------------------------------------------------
	//! Une mort : zone dangereuse
	static void OnDeath(IEntity victim)
	{
		if (victim && s_aDangerPos)
			AddDanger(victim.GetOrigin());
	}

	//------------------------------------------------------------------------------------------------
	protected static void AddDanger(vector position)
	{
		s_aDangerPos.Insert(position);
		s_aDangerTemps.Insert(Now());
		while (s_aDangerPos.Count() > 64)
		{
			s_aDangerPos.RemoveOrdered(0);
			s_aDangerTemps.RemoveOrdered(0);
		}
	}

	//------------------------------------------------------------------------------------------------
	//! D09 : y a-t-il eu un tir ou une mort récente près de ce point ?
	static bool IsDangerous(vector position)
	{
		if (!s_aDangerPos)
			return false;

		float limite = Now() - DANGER_S * 1000;
		for (int i = 0; i < s_aDangerPos.Count(); i++)
		{
			if (s_aDangerTemps[i] < limite)
				continue;
			if (vector.DistanceXZ(s_aDangerPos[i], position) < DANGER_RAYON)
				return true;
		}
		return false;
	}

	//------------------------------------------------------------------------------------------------
	//! Coefficient de dégâts de ce personnage (1 = normal, 0,1 = protégé)
	static float GetDamageFactor(IEntity character)
	{
		if (!s_mProtection)
			return 1;

		float fin;
		if (!s_mProtection.Find(character, fin))
			return 1;

		if (Now() >= fin)
		{
			s_mProtection.Remove(character);
			return 1;
		}

		return PROTECTION_FACTEUR;
	}

	//------------------------------------------------------------------------------------------------
	//! Chaque seconde : viser coupe la protection (D14)
	static void Tick()
	{
		if (!s_mProtection)
			return;

		array<IEntity> fin = {};
		foreach (IEntity character, float temps : s_mProtection)
		{
			ChimeraCharacter chimera = ChimeraCharacter.Cast(character);
			if (!chimera || chimera.IsDeleted())
			{
				fin.Insert(character);
				continue;
			}

			CharacterControllerComponent controller = chimera.GetCharacterController();
			if (controller && controller.IsWeaponADS())
				fin.Insert(character);
		}

		foreach (IEntity retire : fin)
		{
			s_mProtection.Remove(retire);
		}
	}
}

//------------------------------------------------------------------------------------------------
//! D08 : point d'apparition posé à la main dans une ville (étage, toit). À poser 5 cm au-dessus du sol ou du toit.
[EntityEditorProps(category: "SimplePVP", description: "Point d'apparition posé à la main (étage, toit)")]
class SPVP_SpawnPointClass : GenericEntityClass
{
}

class SPVP_SpawnPoint : GenericEntity
{
	protected static ref array<SPVP_SpawnPoint> s_aPoints;

	//------------------------------------------------------------------------------------------------
	void SPVP_SpawnPoint(IEntitySource src, IEntity parent)
	{
		SetEventMask(EntityEvent.INIT);
	}

	//------------------------------------------------------------------------------------------------
	override void EOnInit(IEntity owner)
	{
		if (!s_aPoints)
			s_aPoints = {};
		if (s_aPoints.Find(this) < 0)
			s_aPoints.Insert(this);
	}

	//------------------------------------------------------------------------------------------------
	void ~SPVP_SpawnPoint()
	{
		if (s_aPoints)
			s_aPoints.RemoveItem(this);
	}

	//------------------------------------------------------------------------------------------------
	//! Les points posés dans ce cercle
	static void GetInCircle(vector center, float rayon, notnull array<vector> outPositions)
	{
		if (!s_aPoints)
			return;

		foreach (SPVP_SpawnPoint point : s_aPoints)
		{
			if (point && vector.DistanceXZ(point.GetOrigin(), center) <= rayon)
				outPositions.Insert(point.GetOrigin());
		}
	}
}
