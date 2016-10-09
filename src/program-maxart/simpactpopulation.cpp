#include "simpactpopulation.h"
#include "eventmortality.h"
#include "eventaidsmortality.h"
#include "eventformation.h"
#include "eventdebut.h"
#include "eventchronicstage.h"
#include "eventtransmission.h"
#include "eventhivseed.h"
#include "eventintervention.h"
#include "populationdistribution.h"
#include "person.h"
#include "gslrandomnumbergenerator.h"
#include "util.h"

// TODO: for debugging
#include <iostream>
using namespace std;

SimpactPopulationConfig::SimpactPopulationConfig()
{
	m_initialMen = 100;
	m_initialWomen = 100;
	m_eyeCapsFraction = 1;
}

SimpactPopulationConfig::~SimpactPopulationConfig()
{
}

SimpactPopulation::SimpactPopulation(bool parallel, GslRandomNumberGenerator *pRndGen) : Population(parallel, pRndGen)
{
	m_initialPopulationSize = -1;
	m_eyeCapsFraction = 1;
	m_init = false;
}

SimpactPopulation::~SimpactPopulation()
{
}

bool SimpactPopulation::init(const SimpactPopulationConfig &config, const PopulationDistribution &popDist)
{
	if (m_init)
	{
		setErrorString("Population is already initialized");
		return false;
	}

	double eyeCapsFraction = config.getEyeCapsFraction();
	assert(eyeCapsFraction >= 0 && eyeCapsFraction <= 1.0);

	m_eyeCapsFraction = eyeCapsFraction;

	if (!createInitialPopulation(config, popDist))
		return false;

	if (!scheduleInitialEvents())
		return false;

	m_init = true;
	return true;
}

bool SimpactPopulation::createInitialPopulation(const SimpactPopulationConfig &config, const PopulationDistribution &popDist)
{
	int numMen = config.getInitialMen();
	int numWomen = config.getInitialWomen();

	if (numMen < 0 || numWomen < 0)
	{
		setErrorString("The number of men and women must be at least zero");
		return false;
	}

	// Time zero is at the start of the simulation, so the birth dates are negative

	for (int i = 0 ; i < numMen ; i++)
	{
		double age = popDist.pickAge(true);

		Person *pPerson = new Man(-age);

		if (age > EventDebut::getDebutAge())
			pPerson->setSexuallyActive(0);

		addNewPerson(pPerson);
	}
	
	for (int i = 0 ; i < numWomen ; i++)
	{
		double age = popDist.pickAge(false);

		Person *pPerson = new Woman(-age);
		if (age > EventDebut::getDebutAge())
			pPerson->setSexuallyActive(0);

		addNewPerson(pPerson);
	}

	m_initialPopulationSize = numMen + numWomen;

	return true;
}

bool SimpactPopulation::scheduleInitialEvents()
{
	int numMen = getNumberOfMen();
	int numWomen = getNumberOfWomen();
	int numPeople = getNumberOfPeople();

	Man **ppMen = getMen();
	Woman **ppWomen = getWomen();
	Person **ppPeople = getAllPeople();

	// Initialize the event list with the mortality events
	// both normal and AIDS based
	for (int i = 0 ; i < numPeople ; i++)
	{
		Person *pPerson = ppPeople[i];

		EventMortality *pEvt = new EventMortality(pPerson);
		onNewEvent(pEvt);
	}

	
	// Relationship formation. We'll only process the women, the
	// events for the men are scheduled automatically
	for (int i = 0 ; i < numWomen ; i++)
	{
		Woman *pWoman = ppWomen[i];
		assert(pWoman->getGender() == Person::Female);

		if (pWoman->isSexuallyActive())
			initializeFormationEvents(pWoman);
	}

	// For the people who are not sexually active, set a debut event

	for (int i = 0 ; i < numPeople ; i++)
	{
		Person *pPerson = ppPeople[i];

		if (!pPerson->isSexuallyActive())
		{
			EventDebut *pEvt = new EventDebut(pPerson);
			onNewEvent(pEvt);
		}
	}

	if (EventHIVSeed::getSeedTime() >= 0)
	{
		EventHIVSeed *pEvt = new EventHIVSeed(); // this is a global event
		onNewEvent(pEvt);
	}

	if (EventIntervention::hasNextIntervention()) // We need to schedule a first intervention event
	{
		// Note: the fire time will be determined by the event itself, in the
		//       getNewInternalTimeDifference function
		EventIntervention *pEvt = new EventIntervention(); // global event
		onNewEvent(pEvt);
	}

	return true;
}

void SimpactPopulation::onAboutToFire(EventBase *pEvt)
{
	SimpactEvent *pEvent = static_cast<SimpactEvent *>(pEvt);
	assert(pEvent != 0);

	double t = getTime();
	assert(t >= 0);

//	std::cout << t << "\t" << pEvent->getDescription(t) << std::endl;

	pEvent->writeLogs(t);
}

void SimpactPopulation::initializeFormationEvents(Person *pPerson)
{
	assert(pPerson->isSexuallyActive());
	assert(pPerson->getInfectionStage() != Person::AIDSFinal);

	GslRandomNumberGenerator *pRngGen = getRandomNumberGenerator();

	if (m_eyeCapsFraction >= 1.0)
	{
		if (pPerson->getGender() == Person::Male)
		{
			Man *pMan = MAN(pPerson);
			Woman **ppWomen = getWomen();
			int numWomen = getNumberOfWomen();

			for (int i = 0 ; i < numWomen ; i++)
			{
				Woman *pWoman = ppWomen[i];
				
				// No events will be scheduled of the person
				if (pWoman->isSexuallyActive() && pWoman->getInfectionStage() != Person::AIDSFinal)
				{
					EventFormation *pEvt = new EventFormation(pMan, pWoman, -1);
					onNewEvent(pEvt);
				}
			}
		}
		else // Female
		{
			Woman *pWoman = WOMAN(pPerson);
			Man **ppMen = getMen();
			int numMen = getNumberOfMen();

			for (int i = 0 ; i < numMen ; i++)
			{
				Man *pMan = ppMen[i];

				if (pMan->isSexuallyActive() && pMan->getInfectionStage() != Person::AIDSFinal)
				{
					EventFormation *pEvt = new EventFormation(pMan, pWoman, -1);
					onNewEvent(pEvt);
				}
			}
		}
	}
	else // Use eyecaps
	{
		vector<Person *> interests;

		if (pPerson->getGender() == Person::Male)
		{
			Man *pMan = MAN(pPerson);
			int numWomen = getNumberOfWomen();
			double meanManInterests = m_eyeCapsFraction * (double)numWomen;

			int numInterests = (int)pRngGen->pickPoissonNumber(meanManInterests);
			interests.resize(numInterests);

			getInterestsForPerson(pMan, interests);
			
			for (int i = 0 ; i < numInterests ; i++)
			{
				Person *pWoman = interests[i];
				assert(pWoman->isWoman());

				if (pWoman->isSexuallyActive() && pWoman->getInfectionStage() != Person::AIDSFinal)
				{
					pMan->addPersonOfInterest(pWoman);
					pWoman->addPersonOfInterest(pMan);
				}
			}

			numInterests = pMan->getNumberOfPersonsOfInterest();

			for (int i = 0 ; i < numInterests ; i++)
			{
				Person *pPoi = pMan->getPersonOfInterest(i);

				EventFormation *pEvt = new EventFormation(pMan, pPoi, -1);
				onNewEvent(pEvt);
			}
		}
		else // Female
		{
			Woman *pWoman = WOMAN(pPerson);
			int numMen = getNumberOfMen();
			double meanWomanInterests = m_eyeCapsFraction * (double)numMen;

			int numInterests = (int)pRngGen->pickPoissonNumber(meanWomanInterests);
			interests.resize(numInterests);

			getInterestsForPerson(pWoman, interests);
			
			for (int i = 0 ; i < numInterests ; i++)
			{
				Person *pMan = interests[i];
				assert(pMan->isMan());

				if (pMan->isSexuallyActive() && pMan->getInfectionStage() != Person::AIDSFinal)
				{
					pMan->addPersonOfInterest(pWoman);
					pWoman->addPersonOfInterest(pMan);
				}
			}

			numInterests = pWoman->getNumberOfPersonsOfInterest();

			for (int i = 0 ; i < numInterests ; i++)
			{
				Person *pPoi = pWoman->getPersonOfInterest(i);

				EventFormation *pEvt = new EventFormation(pPoi, pWoman, -1);
				onNewEvent(pEvt);
			}
		}
	}
}

// Doubles and persons who are not sexually active will be removed from the list in another stage
void SimpactPopulation::getInterestsForPerson(const Person *pPerson, vector<Person *> &interests)
{
	GslRandomNumberGenerator *pRngGen = getRandomNumberGenerator();
	Woman **ppWomen = getWomen();
	Man **ppMen = getMen();
	int numWomen = getNumberOfWomen();
	int numMen = getNumberOfMen();
	int numInterests = interests.size();

	if (pPerson->isMan())
	{
		for (int i = 0 ; i < numInterests ; i++)
		{
			int idx = (int)(pRngGen->pickRandomDouble() * (double)numWomen);
			assert(idx >= 0 && idx < numWomen);

			Woman *pWoman = ppWomen[idx];
			interests[i] = pWoman;
		}
	}
	else // Woman
	{
		for (int i = 0 ; i < numInterests ; i++)
		{
			int idx = (int)(pRngGen->pickRandomDouble() * (double)numMen);
			assert(idx >= 0 && idx < numMen);

			Man *pMan = ppMen[idx];
			interests[i] = pMan;
		}
	}
}
