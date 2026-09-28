//------------------------------------------------------------------------------------------------
// SimplePVP — Réglages CRX et ordres des bots (serveur)
// Groupe : offensif, tir à vue, enquête sur les tirs avec fouille des bâtiments, jamais de retour en arrière,
//          combat individuel (un bot = un groupe).
// Soldat : perception (valeurs éprouvées sur la PAG), visée « expert » (I12), jamais de fuite, lampe éteinte.
// Portée : les bots tirent jusqu'à 300 m (I17), y compris sur un joueur sans arme (L14 permet de partir au couteau ;
//          le jeu de base ne tire jamais sur une cible désarmée).
// Livraison « bots »
//------------------------------------------------------------------------------------------------

class SPVP_BotAI
{
	//------------------------------------------------------------------------------------------------
	//! Réglages CRX du groupe et du soldat
	static void Configure(SCR_AIGroup group, IEntity character)
	{
		if (!group || group.IsDeleted() || !character || character.IsDeleted())
			return;

		SCR_AIGroupInfoComponent groupInfo = SCR_AIGroupInfoComponent.Cast(group.FindComponent(SCR_AIGroupInfoComponent));
		if (groupInfo)
		{
			groupInfo.SetCombatBehaviorType(CRX_EAICombatBehaviorType.OFFENSIVE);
			groupInfo.SetCombatMode(CRX_EAICombatMode.RED);
			groupInfo.SetCombatModeAutonomous(false);
			groupInfo.SetCombatMovementType(CRX_EAICombatMovementType.INDIVIDUAL);
			groupInfo.SetCombatMovementTypeAutonomous(false);
			groupInfo.SetWeaponFiredReactionDistance(500);
			groupInfo.SetInvestigate(true);
			groupInfo.SetInvestigateDuration(90);
			groupInfo.SetInvestigateRadius(-1);
			groupInfo.SetInvestigateBuildingSearch(true);
			groupInfo.SetSuppress(true);
			groupInfo.SetReturnToPositionOriginType(CRX_EAIReturnToPositionOriginType.NEVER);
		}

		AIAgent agent = GetAgent(character);
		if (!agent)
			return;

		SCR_AIInfoComponent info = SCR_AIInfoComponent.Cast(agent.FindComponent(SCR_AIInfoComponent));
		if (!info)
			return;

		info.SetPerceptionSafe(1.3);
		info.SetPerceptionVigilant(2.5);
		info.SetPerceptionAlerted(2.5);
		info.SetPerceptionThreatened(3.0);
		info.SetFleeChance(0);
		info.SetFlashlightState(CRX_EAIFlashlightState.OFF);
		info.SetHoldPosition(false);

		// Visée : un bot seul dans son groupe n'est jamais classé par CRX et tirerait parfaitement (erreur 0)
		float erreur = SPVP_Settings.Get().m_fBotsPrecisionErreur;
		info.SetAimAccuracyErrorOriginal(erreur);
		info.SetAimAccuracyError(Math.Clamp(erreur + info.GetAimAccuracyErrorModifier(), 0, 3));
	}

	//------------------------------------------------------------------------------------------------
	//! Ordre : aller voir un point (CRX reprend le rayon et la durée du groupe, réglés juste avant)
	static void Investigate(SCR_AIGroup group, IEntity bot, vector position, float rayon, float duree)
	{
		SCR_AIUtilityComponent utility = GetUtility(bot);
		if (!utility)
			return;

		if (group && !group.IsDeleted())
		{
			SCR_AIGroupInfoComponent groupInfo = SCR_AIGroupInfoComponent.Cast(group.FindComponent(SCR_AIGroupInfoComponent));
			if (groupInfo)
			{
				groupInfo.SetInvestigateRadius(rayon);
				groupInfo.SetInvestigateDuration(duree);
			}
		}

		utility.SetStateAllActionsOfType(SCR_AIMoveAndInvestigateBehavior, EAIActionState.FAILED);
		utility.AddAction(new SCR_AIMoveAndInvestigateBehavior(utility, null, position, SCR_AIActionBase.PRIORITY_BEHAVIOR_MOVE_AND_INVESTIGATE, SCR_AIActionBase.PRIORITY_LEVEL_NORMAL, rayon, true, EAIUnitType.UnitType_Infantry, duree));
	}

	//------------------------------------------------------------------------------------------------
	//! Le bot a-t-il une cible en ce moment ?
	static bool HasTarget(IEntity bot)
	{
		SCR_AIUtilityComponent utility = GetUtility(bot);
		if (!utility || !utility.m_CombatComponent)
			return false;

		return utility.m_CombatComponent.GetCurrentTarget() != null;
	}

	//------------------------------------------------------------------------------------------------
	static AIAgent GetAgent(IEntity character)
	{
		if (!character)
			return null;

		AIControlComponent control = AIControlComponent.Cast(character.FindComponent(AIControlComponent));
		if (!control)
			return null;

		return control.GetControlAIAgent();
	}

	//------------------------------------------------------------------------------------------------
	static SCR_AIUtilityComponent GetUtility(IEntity character)
	{
		AIAgent agent = GetAgent(character);
		if (!agent)
			return null;

		return SCR_AIUtilityComponent.Cast(agent.FindComponent(SCR_AIUtilityComponent));
	}
}

//------------------------------------------------------------------------------------------------
//! I17 et L14 : portée de tir des bots, et tir permis sur une cible sans arme
modded class SCR_AICombatComponent
{
	//------------------------------------------------------------------------------------------------
	override void SetTargetSelectionProperties(bool closeCombat)
	{
		super.SetTargetSelectionProperties(closeCombat);
		if (!Replication.IsServer())
			return;

		float portee = SPVP_Settings.Get().m_fBotsPorteeTir;
		if (closeCombat)
		{
			m_WeaponTargetSelector.SetSelectionProperties(TARGET_MAX_LAST_SEEN_DIRECT_ATTACK_CLOSE, TARGET_MAX_LAST_SEEN_INDIRECT_ATTACK_CLOSE, TARGET_MAX_LAST_SEEN_INDIRECT_ATTACK_CLOSE,
				TARGET_MIN_INDIRECT_TRACE_FRACTION_MIN, portee, TARGET_MAX_DISTANCE_VEHICLE, TARGET_MAX_TIME_SINCE_ENDANGERED, portee);
			return;
		}

		m_WeaponTargetSelector.SetSelectionProperties(TARGET_MAX_LAST_SEEN_DIRECT_ATTACK, TARGET_MAX_LAST_SEEN_INDIRECT_ATTACK, TARGET_MAX_LAST_SEEN,
			TARGET_MIN_INDIRECT_TRACE_FRACTION_MIN, portee, TARGET_MAX_DISTANCE_VEHICLE, TARGET_MAX_TIME_SINCE_ENDANGERED, portee);
	}
}
