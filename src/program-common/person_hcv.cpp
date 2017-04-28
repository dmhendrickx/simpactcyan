#include "person_hcv.h"
#include "person.h"
#include "configsettings.h"
#include "configwriter.h"
#include "configdistributionhelper.h"
#include "configfunctions.h"
#include "jsonconfig.h"
#include <vector>
#include <iostream>

using namespace std;

Person_HCV::Person_HCV(Person *pSelf) : m_pSelf(pSelf)
{
	assert(pSelf);

	m_infectionTime = -1e200; // not set
	m_pInfectionOrigin = 0;
	m_infectionType = None;

	m_hazardAParam = m_pADist->pickNumber();
	m_hazardB4Param = m_pB4Dist->pickNumber();
}

Person_HCV::~Person_HCV()
{
}

void Person_HCV::setInfected(double t, Person *pOrigin, InfectionType iType)
{ 
	assert(iType != None);
	assert(!(pOrigin == 0 && iType != Seed));

	m_infectionTime = t; 
	m_pInfectionOrigin = pOrigin;
	m_infectionType = iType;

	//cout << "Person_HCV seeding " << m_pSelf->getName() << endl;
}

ProbabilityDistribution *Person_HCV::m_pADist = 0;
ProbabilityDistribution *Person_HCV::m_pB4Dist = 0;

void Person_HCV::processConfig(ConfigSettings &config, GslRandomNumberGenerator *pRndGen)
{
	assert(pRndGen != 0);

	delete m_pADist;
	delete m_pB4Dist;
	m_pADist = getDistributionFromConfig(config, pRndGen, "person.hcv.a");
	m_pB4Dist = getDistributionFromConfig(config, pRndGen, "person.hcv.b4");
}

void Person_HCV::obtainConfig(ConfigWriter &config)
{
	addDistributionToConfig(m_pADist, config, "person.hcv.a");
	addDistributionToConfig(m_pB4Dist, config, "person.hcv.b4");
}

ConfigFunctions personHCVConfigFunctions(Person_HCV::processConfig, Person_HCV::obtainConfig, "Person_HCV");

JSONConfig personHCVJSONConfig(R"JSON(

        "PersonHCV": {
            "depends": null,
            "params": [ 
				[ "person.hcv.a.dist", "distTypes", [ "fixed", [ [ "value", 0 ]   ] ] ],
				[ "person.hcv.b4.dist", "distTypes", [ "fixed", [ [ "value", 0 ]   ] ] ]
            ],
            "info": [
				"The 'a' parameter in the HCV transmission hazard is chosen from this",
				"distribution, allowing transmission to depend more on the individual",
				"The 'b4' parameter in the HCV transmission hazard is chosen from this",
				"distribution, allowing transmission to",
				"depend more on susceptibility for HCV only."
            ]
        })JSON");

