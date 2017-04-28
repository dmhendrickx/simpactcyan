#ifndef PERSON_HCV_H

#define PERSON_HCV_H

#include "util.h"
#include <assert.h>

class Person;
class ProbabilityDistribution;
class ConfigSettings;
class ConfigWriter;
class GslRandomNumberGenerator;

class Person_HCV
{
public:
	enum InfectionType { None, Partner, Seed };

	Person_HCV(Person *pSelf);
	~Person_HCV();

	InfectionType getInfectionType() const											{ return m_infectionType; }
	void setInfected(double t, Person *pOrigin, InfectionType iType);

	bool isInfected() const															{ if (m_infectionType == None) return false; return true; }
	double getInfectionTime() const													{ assert(isInfected()); return m_infectionTime; }
	Person *getInfectionOrigin() const												{ assert(isInfected()); return m_pInfectionOrigin; }

	double getHazardAParameter() const												{ return m_hazardAParam; }
	double getHazardB4Parameter() const												{ return m_hazardB4Param; }

	static void processConfig(ConfigSettings &config, GslRandomNumberGenerator *pRndGen);
	static void obtainConfig(ConfigWriter &config);
private:
	const Person *m_pSelf;

	double m_infectionTime;
	Person *m_pInfectionOrigin;
	InfectionType m_infectionType;
	double m_hazardAParam;
	double m_hazardB4Param;

	static ProbabilityDistribution *m_pADist;
	static ProbabilityDistribution *m_pB4Dist;
};

#endif // PERSON_HCV_H
