#include "eventhcvseed.h"
#include "eventhcvtransmission.h"
#include "gslrandomnumbergenerator.h"
#include "jsonconfig.h"
#include "configfunctions.h"
#include "util.h"
#include <iostream>
#include <cmath>

using namespace std;

EventHCVSeed::EventHCVSeed()
{
}

EventHCVSeed::~EventHCVSeed()
{
}

double EventHCVSeed::getNewInternalTimeDifference(GslRandomNumberGenerator *pRndGen, const State *pState)
{
	return EventSeedBase::getNewInternalTimeDifference(s_settings, pRndGen, pState);
}

string EventHCVSeed::getDescription(double tNow) const
{
	return "HCV seeding";
}

void EventHCVSeed::writeLogs(const SimpactPopulation &pop, double tNow) const
{
	writeEventLogStart(true, "HCV seeding", tNow, 0, 0);
}

void EventHCVSeed::fire(Algorithm *pAlgorithm, State *pState, double t)
{
	EventSeedBase::fire(s_settings, t, pState, EventHCVTransmission::infectPerson);
}

SeedEventSettings EventHCVSeed::s_settings;

void EventHCVSeed::processConfig(ConfigSettings &config, GslRandomNumberGenerator *pRndGen)
{
	EventSeedBase::processConfig(s_settings, config, pRndGen, "hcvseed");
}

void EventHCVSeed::obtainConfig(ConfigWriter &config)
{
	EventSeedBase::obtainConfig(s_settings, config, "hcvseed");
}

ConfigFunctions hcvSeedingConfigFunctions(EventHCVSeed::processConfig, EventHCVSeed::obtainConfig, "EventHCVSeed");

// The -1 is the default seed time; a negative value means it's disabled
JSONConfig hcvseedingJSONConfig(EventSeedBase::getJSONConfigText("EventHCVSeeding", "hcvseed", "HCV", -1));
