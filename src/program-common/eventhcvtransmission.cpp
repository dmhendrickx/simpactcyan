#include "eventhcvtransmission.h"
#include "jsonconfig.h"
#include "configfunctions.h"
#include "util.h"
#include <cmath>
#include <iostream>

using namespace std;

// Conception happens between two people, so using this constructor seems natural.
// Also, when one of the involved persons dies before this is fired, the event is
// removed automatically.
EventHCVTransmission::EventHCVTransmission(Person *pPerson1, Person *pPerson2) : SimpactEvent(pPerson1, pPerson2)
{
	// is about transmission from pPerson1 to pPerson2, so no ordering according to
	// gender here
	assert(pPerson1->hcv().isInfected() && !pPerson2->hcv().isInfected());
}

EventHCVTransmission::~EventHCVTransmission()
{
}

string EventHCVTransmission::getDescription(double tNow) const
{
	return strprintf("HCV Transmission event from %s to %s", getPerson(0)->getName().c_str(), getPerson(1)->getName().c_str());
}

void EventHCVTransmission::writeLogs(const SimpactPopulation &pop, double tNow) const
{
	Person *pPerson1 = getPerson(0);
	Person *pPerson2 = getPerson(1);
	writeEventLogStart(true, "HCV transmission", tNow, pPerson1, pPerson2);
}

// The dissolution event that makes this event useless involves the exact same people,
// so this function will automatically make sure that this conception event is discarded
// (this function is definitely called for those people)

bool EventHCVTransmission::isUseless(const PopulationStateInterface &population)
{
	// Transmission from pPerson1 to pPerson2
	Person *pPerson1 = getPerson(0);
	Person *pPerson2 = getPerson(1);

	// If person2 already became HCV positive, there no sense in further transmission
	if (pPerson2->hcv().isInfected())
		return true;

	// Event is useless if the relationship between the two people is over
	if (!pPerson1->hasRelationshipWith(pPerson2))
	{
		assert(!pPerson2->hasRelationshipWith(pPerson1));
		return true;
	}

	// Make sure the two lists are consistent: if person1 has a relationship with person2, person2
	// should also have a relationship with person1
	assert(pPerson2->hasRelationshipWith(pPerson1));

	return false;
}

void EventHCVTransmission::infectPerson(SimpactPopulation &population, Person *pOrigin, Person *pTarget, double t)
{
	assert(!pTarget->hcv().isInfected());

	if (pOrigin == 0) // Seeding
		pTarget->hcv().setInfected(t, 0, Person_HCV::Seed);
	else
	{
		assert(pOrigin->hcv().isInfected());
		pTarget->hcv().setInfected(t, pOrigin, Person_HCV::Partner);
	}

	// Check relationships pTarget is in, and if the partner is not yet infected, schedule
	// a transmission event.
	int numRelations = pTarget->getNumberOfRelationships();
	pTarget->startRelationshipIteration();

	for (int i = 0; i < numRelations; i++)
	{
		double formationTime = -1;
		Person *pPartner = pTarget->getNextRelationshipPartner(formationTime);

		if (!pPartner->hcv().isInfected())
		{
			EventHCVTransmission *pEvtTrans = new EventHCVTransmission(pTarget, pPartner);
			population.onNewEvent(pEvtTrans);
		}
	}

#ifndef NDEBUG
	double tDummy;
	assert(pTarget->getNextRelationshipPartner(tDummy) == 0);
#endif // NDEBUG
}

void EventHCVTransmission::fire(Algorithm *pAlgorithm, State *pState, double t)
{
	SimpactPopulation &population = SIMPACTPOPULATION(pState);
	// Transmission from pPerson1 to pPerson2
	Person *pPerson1 = getPerson(0);
	Person *pPerson2 = getPerson(1);

	// Person 1 should be infected , person 2 should not be infected yet
	assert(pPerson1->hcv().isInfected());
	assert(!pPerson2->hcv().isInfected());

	infectPerson(population, pPerson1, pPerson2, t);
}

double EventHCVTransmission::calculateInternalTimeInterval(const State *pState, double t0, double dt)
{
	Person *pOrigin = getPerson(0);
	Person *pTarget = getPerson(1);
	double tMax = getTMax(pOrigin, pTarget);

	HazardFunctionHCVTransmission h0(pOrigin, pTarget);
	TimeLimitedHazardFunction h(h0, tMax);

	return h.calculateInternalTimeInterval(t0, dt);
}

double EventHCVTransmission::solveForRealTimeInterval(const State *pState, double Tdiff, double t0)
{
	Person *pOrigin = getPerson(0);
	Person *pTarget = getPerson(1);
	double tMax = getTMax(pOrigin, pTarget);

	HazardFunctionHCVTransmission h0(pOrigin, pTarget);
	TimeLimitedHazardFunction h(h0, tMax);

	return h.solveForRealTimeInterval(t0, Tdiff);
}

double EventHCVTransmission::s_tMax = 200;
double EventHCVTransmission::s_c1 = 0;
double EventHCVTransmission::s_c2 = 0;
double EventHCVTransmission::s_d = 0;
double EventHCVTransmission::s_e1 = 0;
double EventHCVTransmission::s_e2 = 0;
double EventHCVTransmission::HazardFunctionHCVTransmission::s_b = 0;

void EventHCVTransmission::processConfig(ConfigSettings &config, GslRandomNumberGenerator *pRndGen)
{
	bool_t r;

	if (!(r = config.getKeyValue("hcvtransmission.hazard.b", HazardFunctionHCVTransmission::s_b)) ||
		!(r = config.getKeyValue("hcvtransmission.hazard.c1", s_c1)) ||
		!(r = config.getKeyValue("hcvtransmission.hazard.c2", s_c1)) ||
		!(r = config.getKeyValue("hcvtransmission.hazard.d", s_d)) ||
		!(r = config.getKeyValue("hcvtransmission.hazard.e1", s_e1)) ||
		!(r = config.getKeyValue("hcvtransmission.hazard.e2", s_e2)) ||
		!(r = config.getKeyValue("hcvtransmission.hazard.t_max", s_tMax))
		)
		abortWithMessage(r.getErrorString());
}

void EventHCVTransmission::obtainConfig(ConfigWriter &config)
{
	bool_t r;

	if (!(r = config.addKey("hcvtransmission.hazard.b", HazardFunctionHCVTransmission::s_b)) ||
		!(r = config.addKey("hcvtransmission.hazard.c1", s_c1)) ||
		!(r = config.addKey("hcvtransmission.hazard.c2", s_c2)) ||
		!(r = config.addKey("hcvtransmission.hazard.d", s_d)) ||
		!(r = config.addKey("hcvtransmission.hazard.e1", s_e1)) ||
		!(r = config.addKey("hcvtransmission.hazard.e2", s_e2)) ||
		!(r = config.addKey("hcvtransmission.hazard.t_max", s_tMax))
		)
		abortWithMessage(r.getErrorString());
}

double EventHCVTransmission::getTMax(const Person *pPerson1, const Person *pPerson2)
{
	assert(pPerson1 != 0 && pPerson2 != 0);

	double tb1 = pPerson1->getDateOfBirth();
	double tb2 = pPerson2->getDateOfBirth();

	double tMax = tb1;

	if (tb2 < tMax)
		tMax = tb2;

	assert(s_tMax > 0);
	tMax += s_tMax;
	return tMax;
}

int EventHCVTransmission::getHi(const Person *pPerson1)
{
	assert(pPerson1 != 0);
	bool H1 = pPerson1->hiv().isInfected();
	int Hi = 0;
	if (H1 == true)
		Hi = 1;
	return Hi;
}

int EventHCVTransmission::getHj(const Person *pPerson2)
{
	assert(pPerson2 != 0);
	bool H2 = pPerson2->hiv().isInfected();
	int Hj = 0;
	if (H2 == true)
		Hj = 1;
	return Hj;
}

int EventHCVTransmission::getM(const Person *pPerson1)
{
	assert(pPerson1 != 0);
	bool M1 = pPerson1->isMan();
	int M = 0;
	if (M1 == true)
		M = 1;
	return M;
}

EventHCVTransmission::HazardFunctionHCVTransmission::HazardFunctionHCVTransmission(const Person *pPerson1,
	const Person *pPerson2)
	: HazardFunctionExp(getA(pPerson1, pPerson2), s_b)
{
}

EventHCVTransmission::HazardFunctionHCVTransmission::~HazardFunctionHCVTransmission()
{
}

double EventHCVTransmission::HazardFunctionHCVTransmission::getA(const Person *pOrigin, const Person *pTarget)
{
	assert(pOrigin);
	assert(pTarget);
	return pOrigin->hcv().getHazardAParameter() - s_b*pOrigin->hcv().getInfectionTime() + s_c1*EventHCVTransmission::getHi(pOrigin) + s_c2*EventHCVTransmission::getHj(pTarget) + s_d*EventHCVTransmission::getM(pOrigin) + s_e1*pTarget->hiv().getHazardB3Parameter() + s_e2*pTarget->hcv().getHazardB4Parameter();
}

ConfigFunctions hcvTransmissionConfigFunctions(EventHCVTransmission::processConfig, EventHCVTransmission::obtainConfig,
	"EventHCVTransmission");

JSONConfig hcvTransmissionJSONConfig(R"JSON(
        "EventHCVTransmission": { 
            "depends": null,
            "params": [ 
				[ "hcvtransmission.hazard.b", 0 ],
				[ "hcvtransmission.hazard.c1", 0 ],
				[ "hcvtransmission.hazard.c2", 0 ],
				[ "hcvtransmission.hazard.d", 0 ],
				[ "hcvtransmission.hazard.e1", 0 ],
				[ "hcvtransmission.hazard.e2", 0 ],
				[ "hcvtransmission.hazard.t_max", 200 ]
			],
            "info": [ 
				"These configuration parameters allow you to set the 'b', 'c' and 'd' values in the hazard",
				" h = exp(a_i + b*(t-t_infected)+ c1*H_i + c2*H_j + d*M_i + e1*b3_j + e2*b4_j)",
				"The value of 'a_i' depends on the individual, and can be specified as a ",
				"distribution in the person parameters ",
				"The value of 'b3_j' depends on the individual, and can be specified as a ",
				"distribution in the person parameters ",
				"The value of 'b4_j' depends on the individual, and can be specified as a ",
				"distribution in the person parameters."
            ]
        })JSON");