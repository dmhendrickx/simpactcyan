#include "facilities.h"
#include "configsettings.h"
#include "configwriter.h"
#include "jsonconfig.h"
#include "configfunctions.h"
#include "util.h"
#include "point2d.h"
#include <vector>
#include <limits>
#include <cmath>
#include <map>

#include <iostream>

double meanEarthRadius = 6371.0;

double toRad(double deg)
{
	return deg/180.0*M_PI;
}

using namespace std;

Facilities::Facilities(const vector<Facility> &f)
{
	m_facilities = f;

	int maxStep = numeric_limits<int>::min();
	for (size_t i = 0 ; i < f.size() ; i++)
	{
		int step = f[i].getRandomizationStep();
		if (maxStep < step)
			maxStep = step;
	}

	m_numSteps = maxStep+1;
}

Facilities::~Facilities()
{
}

void Facilities::getFacilitiesForRandomizationStep(int step, vector<Facility *> &f)
{
	assert(step >= 0 && step < m_numSteps);

	f.clear();
	for (size_t i = 0 ; i < m_facilities.size() ; i++)
	{
		Facility *pFac = &(m_facilities[i]);

		if (pFac->getRandomizationStep() == step)
			f.push_back(pFac);
	}
}

void Facilities::dump()
{
	for (size_t i = 0 ; i < m_facilities.size() ; i++)
		cout << m_facilities[i].getName() << "," << m_facilities[i].getPosition().x << "," << m_facilities[i].getPosition().y << endl;

	for (int i = 0 ; i < m_numSteps ; i++)
	{
		vector<Facility *> v;

		getFacilitiesForRandomizationStep(i, v);
		cout << (i+1);
		for (int j = 0 ; j < v.size() ; j++)
			cout << "," << v[j]->getName();

		cout << endl;
	}
}

double Facilities::s_startLongitude = numeric_limits<double>::quiet_NaN();
double Facilities::s_startLattitude = numeric_limits<double>::quiet_NaN();
string Facilities::s_corner;
Facilities *Facilities::s_pInstance = 0;

void Facilities::processConfig(ConfigSettings &config, GslRandomNumberGenerator *pRndGen)
{
	vector<string> allowedValues { "top", "bottom" };
	string coordsFile, randFile;

	if (!config.getKeyValue("facilities.geo.start.longitude", s_startLongitude) ||
	    !config.getKeyValue("facilities.geo.start.lattitude", s_startLattitude) ||
		!config.getKeyValue("facilities.geo.start.corner", s_corner, allowedValues) ||
		!config.getKeyValue("facilities.geo.coords", coordsFile) ||
		!config.getKeyValue("facilities.randomization", randFile) )
		abortWithMessage(config.getErrorString());

	//cout << "startLong: " << s_startLongitude << endl;
	//cout << "startLatt: " << s_startLattitude << endl;

	map<string, Point2D> facilityCoords;
	map<string, int> randomization;

	// Read the file with the GPS coords
	{
		FILE *pFile = fopen(coordsFile.c_str(), "rt");
		if (!pFile)
			abortWithMessage("Unable to open file " + coordsFile);

		string line;
		if (!ReadInputLine(pFile, line))
			abortWithMessage("Unable to read CSV header line in " + coordsFile);

		while (ReadInputLine(pFile, line))
		{
			trim(line);
			if (line.length() == 0)
				continue;
			
			vector<string> parts;
			
			SplitLine(line, parts, ",", "\"", "", false);
			if (parts.size() != 3)
				abortWithMessage("Should be 3 columns in CSV file " + coordsFile);
			
			string facilityName = trim(parts[0]);
			double x, y;

			if (!parseAsDouble(parts[1], y) || !parseAsDouble(parts[2], x))
				abortWithMessage("Can't interpret '" + parts[1] + "' or '" + parts[2] + "' as a number in file " + coordsFile);

			if (x < -180.0 || x > 180.0 || y < -90.0 || y > 90.0)
				abortWithMessage("Can't interpret '" + parts[1] + "' or '" + parts[2] + "' as a valid longitude and lattitude in file " + coordsFile);

			// TODO: what are reasonable limits for the approximation to work?
			if (std::abs(y - s_startLattitude) > 3.0 || std::abs(x - s_startLongitude) > 3.0 * std::cos(toRad(s_startLattitude)))
				abortWithMessage("Coordinates '" + parts[1] + "' or '" + parts[2] + "' lie too far from start coordinates for flat approximation to work in file " + coordsFile);

			double X, Y;

			if (s_corner == "top")
			{
				X = toRad( x - s_startLongitude ) * cos(toRad(s_startLattitude)) * meanEarthRadius;
				Y = toRad( s_startLattitude - y ) * meanEarthRadius;
			}
			else
			{
				X = toRad( x - s_startLongitude ) * cos(toRad(s_startLattitude)) * meanEarthRadius;
				Y = toRad( y - s_startLattitude ) * meanEarthRadius;
			}

			facilityCoords[facilityName] = Point2D(X, Y);
		}
		fclose(pFile);
	}

	// Read the file with the randomization
	{
		FILE *pFile = fopen(randFile.c_str(), "rt");
		if (!pFile)
			abortWithMessage("Unable to open file " + randFile);

		string line;
		if (!ReadInputLine(pFile, line))
			abortWithMessage("Unable to read CSV header line in " + randFile);

		while (ReadInputLine(pFile, line))
		{
			line = trim(line);
			if (line.length() == 0)
				continue;

			vector<string> parts;
			SplitLine(line, parts, ",", "\"", "", false);
		
			if (parts.size() != 2)
				abortWithMessage("Expecting two columns in file '" + randFile + "'");

			int step;
			if (!parseAsInt(parts[0], step))
				abortWithMessage("Can't interpret '" + parts[0] + "' as a step number in file '" + randFile + "'");

			vector<string> facilities;
			SplitLine(parts[1], facilities, ",", "", "", false);

			if (facilities.size() == 0)
				abortWithMessage("No facilities present for step number " + intToString(step) + " in file '" + randFile + "'");

			for (size_t i = 0 ; i < facilities.size() ; i++)
			{
				string facility = trim(facilities[i]);

				if (facility.length() == 0)
					abortWithMessage("Empty facility name for step step number " + intToString(step) + " in file '" + randFile + "'");

				if (randomization.find(facility) != randomization.end())
					abortWithMessage("Facility '" + facility + "' occurs at least twice in file '" + randFile + "'");

				randomization[facility] = step;
			}
		}

		fclose(pFile);
	}

	// Perform checks on randomization and geo info
	{
		// Check that the names in the coords file are also found in the randomization file
		for (auto it = facilityCoords.begin() ; it != facilityCoords.end() ; it++)
		{
			string name = it->first;
			if (randomization.find(name) == randomization.end())
				abortWithMessage("Facility '" + name + "' is mentioned in coords file, but is not present in randomization");
		}

		int minStep = numeric_limits<int>::max();
		int maxStep = numeric_limits<int>::min();
		map<int, bool> stepMap;

		// Check other way around, and keep track of the step numbers
		for (auto it = randomization.begin() ; it != randomization.end() ; it++)
		{
			string name = it->first;
			if (facilityCoords.find(name) == facilityCoords.end())
				abortWithMessage("Facility '" + name + "' is mentioned in randomization file, but is not present in coords file");

			int step = it->second;

			if (step < minStep)
				minStep = step;
			if (step > maxStep)
				maxStep = step;

			stepMap[step] = true;
		}

		// Check that lowest step number is 1, and that all steps are used
		if (minStep != 1)
			abortWithMessage("The minimum step value that appears in the randomization file must be '1'");

		for (int s = minStep ; s <= maxStep ; s++)
		{
			if (stepMap.find(s) == stepMap.end())
				abortWithMessage("Expecting step '" + intToString(s) + "' but it doesn't appear to be present in randomization file");
		}
	}

	// Ok, everything seems to be in order, save it
	vector<Facility> facilities;
	
	for (auto it = facilityCoords.begin() ; it != facilityCoords.end() ; it++)
	{
		string name = it->first;
		Point2D coord = it->second;
		int step = randomization[name];
		
		facilities.push_back(Facility(name, coord, step-1)); // we'll start counting from 0 from here on
	}

	delete s_pInstance;
	s_pInstance = new Facilities(facilities);

	//s_pInstance->dump();
}

void Facilities::obtainConfig(ConfigWriter &config)
{
	if (!config.addKey("facilities.geo.start.longitude", s_startLongitude) ||
	    !config.addKey("facilities.geo.start.lattitude", s_startLattitude) ||
		!config.addKey("facilities.geo.start.corner", s_corner) ||
		!config.addKey("facilities.geo.coords", "IGNORE") ||
		!config.addKey("facilities.randomization", "IGNORE") )
		abortWithMessage(config.getErrorString());
}

ConfigFunctions facilitiesConfigFunctions(Facilities::processConfig, Facilities::obtainConfig, "Facilities");

JSONConfig facilitiesJSONConfig(R"JSON(
        "Facilities": {
            "depends": null,
            "params": [
                [ "facilities.geo.coords", "${SIMPACT_DATA_DIR}maxart-facilities.csv" ],
                [ "facilities.geo.start.lattitude", -25.7172 ],
                [ "facilities.geo.start.longitude", 30.7901 ],
                [ "facilities.geo.start.corner", "top", [ "top", "bottom" ] ],
                [ "facilities.randomization", "${SIMPACT_DATA_DIR}maxart-randomization.csv" ]
            ],
            "info": [
                "TODO"
            ]
        })JSON");
