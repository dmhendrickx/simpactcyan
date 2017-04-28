#ifndef EVENTHCVTRANSMISSION_H

#define EVENTHCVTRANSMISSION_H

#include "simpactevent.h"
#include "hazardfunctionexp.h"

class ConfigSettings;

class EventHCVTransmission : public SimpactEvent
{
public:
	// Transmission from person1 onto person2
	EventHCVTransmission(Person *pPerson1, Person *pPerson2);
	~EventHCVTransmission();

	std::string getDescription(double tNow) const;
	void writeLogs(const SimpactPopulation &pop, double tNow) const;

	void fire(Algorithm *pAlgorithm, State *pState, double t);

	static void processConfig(ConfigSettings &config, GslRandomNumberGenerator *pRndGen);
	static void obtainConfig(ConfigWriter &config);

	static void infectPerson(SimpactPopulation &population, Person *pOrigin, Person *pTarget, double t);
protected:
	double calculateInternalTimeInterval(const State *pState, double t0, double dt);
	double solveForRealTimeInterval(const State *pState, double Tdiff, double t0);
	bool isUseless(const PopulationStateInterface &population) override;
	double calculateHazardFactor(const SimpactPopulation &population, double t0);
	class HazardFunctionHCVTransmission : public HazardFunctionExp
	{
	public:
		HazardFunctionHCVTransmission(const Person *pPerson1, const Person *pPerson2);
		~HazardFunctionHCVTransmission();

		static double getA(const Person *pPerson1, const Person *pPerson2);
		static double s_b;
	};
	static double getTMax(const Person *pOrigin, const Person *pTarget);
	static int getHi(const Person *pPerson1);
	static int getHj(const Person *pPerson2);
	static int getM(const Person *pPerson1);
	static double s_tMax;
	static double s_c1;
	static double s_c2;
	static double s_d;
	static double s_e1;
	static double s_e2;
};

#endif // EVENTHCVTRANSMISSION_H