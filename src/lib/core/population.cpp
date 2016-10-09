#ifndef DISABLEOPENMP
#include <omp.h>
#endif // !DISABLEOPENMP
#include "parallel.h"
#include "population.h"
#include "personbase.h"
#include "debugwarning.h"
#include "util.h"
#include <stdlib.h>
#include <stdio.h>
#include <iostream>

// FOR DEBUGGING
#include <map>

// For debugging: undefine to always recalculate all events
//#define POPULATION_ALWAYS_RECALCULATE

#ifdef POPULATION_ALWAYS_RECALCULATE
#define POPULATION_ALWAYS_RECALCULATE_FLAG 1
#else
#define POPULATION_ALWAYS_RECALCULATE_FLAG 0
#endif

#ifndef SIMPLEMNRM
Population::Population(bool parallel, GslRandomNumberGenerator *pRnd) : State(pRnd)
#else
Population::Population(bool parallel, GslRandomNumberGenerator *pRnd) : SimpleState(parallel, pRnd) 
#endif
{
#ifdef DISABLEOPENMP
	if (parallel)
		abortWithMessage("Parallel version requested but OpenMP was not available when creating the program");
#endif // DISABLEOPENMP

	m_numMen = 0; 
	m_numWomen = 0;
	m_numGlobalDummies = 1;

#ifndef SIMPLEMNRM
	std::cerr << "# mNRM: using advanced algorithm" << std::endl;

	m_parallel = parallel;
#else
	std::cerr << "# mNRM: using basic algorithm" << std::endl;

	m_parallel = false; // avoid unnecessary locking
#endif // SIMPLEMNRM
#ifdef NDEBUG
	std::cerr << "# Release version" << std::endl;
#else
	std::cerr << "# Debug version" << std::endl;
#endif // NDEBUG

	m_nextEventID = 0;
	m_nextPersonID = 0;

	if (m_parallel)
	{
#ifndef DISABLEOPENMP
		std::cerr << "# Population: using parallel version with " << omp_get_max_threads() << " threads" << std::endl;
		m_tmpEarliestEvents.resize(omp_get_max_threads());
		m_tmpEarliestTimes.resize(m_tmpEarliestEvents.size());

		m_eventMutexes.resize(256); // TODO: what is a good size here?
		m_personMutexes.resize(256); // TODO: same

		// TODO: in windows it seems that the omp mutex initialization is not ok
#endif // !DISABLEOPENMP
	}

#ifdef DISABLE_PARALLEL
	DEBUGWARNING("#pragma omp is disabled")
#endif // DISABLE_PARALLEL

	// Note: we can't do this too soon since m_nextPersonID must be initialized
	m_people.resize(m_numGlobalDummies);

	for (int i = 0 ; i < m_numGlobalDummies ; i++)
	{
		m_people[i] = new GlobalEventDummyPerson();

		int64_t id = getNextPersonID();
		m_people[i]->setPersonID(id);
	}
}

Population::~Population()
{
	// TODO: free memory!
}

// Each loop we'll delete events that may be deleted
void Population::onAlgorithmLoop()
{
	if (m_eventsToRemove.size() < 10000) // Don't do this too often?
		return;

	for (int i = 0 ; i < m_eventsToRemove.size() ; i++)
		delete m_eventsToRemove[i];
	m_eventsToRemove.resize(0);
}

void Population::setPersonDied(PersonBase *pPerson)
{
	assert(pPerson != 0);
	assert(pPerson->getGender() != PersonBase::GlobalEventDummy);

	// Set the time of death
	pPerson->setTimeOfDeath(getTime());	

	// Remove this person from the living people list
	// we'll just move him/her to another list, since
	// an SimpactEvent::isNoLongerUseful() call may
	// refer to this person and we must be able to let
	// it know that the person has died.

	int listIndex = pPerson->getListIndex();

	assert(listIndex >= 0 && listIndex < m_people.size());
	assert(m_people[listIndex] == pPerson);

	if (pPerson->getGender() == PersonBase::Female) // second part of the list
	{
		int lastFemaleIdx = m_numGlobalDummies + m_numMen + m_numWomen - 1;

		assert(lastFemaleIdx >= 0 && lastFemaleIdx+1 == m_people.size());

		if (lastFemaleIdx != listIndex) // we need to move someone
		{
			PersonBase *pWoman = m_people[lastFemaleIdx];

			m_people[listIndex] = pWoman;
			pWoman->setListIndex(listIndex);

			assert(listIndex >= (m_numMen+m_numGlobalDummies)); // in m_people there are first a number of global event dummies, then men, then a number of women

			// m_firstEventTracker.taint(listIndex);
		}

		m_people.resize(lastFemaleIdx);
		m_numWomen--;

		// m_firstEventTracker.removedLast();
	}
	else // Male
	{
		assert(pPerson->getGender() == PersonBase::Male);

		int lastMaleIdx = m_numGlobalDummies + m_numMen - 1;

		assert(lastMaleIdx >= 0 && lastMaleIdx < m_people.size());

		// First, we're going to rearrange the males, afterwards
		// we'll need to move a female as well, to fill the space
		// that has become available

		if (lastMaleIdx != listIndex)
		{
			PersonBase *pMan = m_people[lastMaleIdx];

			m_people[listIndex] = pMan;
			pMan->setListIndex(listIndex);

			// m_firstEventTracker.taint(listIndex);
		}

		m_numMen--;

		if (m_numWomen > 0)
		{
			int newIdx = m_numMen + m_numGlobalDummies;
			int lastFemaleIdx = m_numGlobalDummies + m_numMen + m_numWomen; // no -1 here since we've already decremented m_numMen

			PersonBase *pWoman = m_people[lastFemaleIdx];

			m_people[newIdx] = pWoman;
			pWoman->setListIndex(newIdx);

			// m_firstEventTracker.tain(m_numMen);
		}

		m_people.resize(m_numGlobalDummies+m_numMen+m_numWomen);

		// m_firstEventTracker.removedLast();
	}

	pPerson->setListIndex(-1); // not needed for the deceased list
	m_deceasedPersons.push_back(pPerson);
}

bool Population::initEventTimes() const
{
	// All event times should already be initialized, this function should not
	// be called
	setErrorString("Separate event time initialization not supported in this implementation, events should already be initialized");
	return false;
}

#ifndef SIMPLEMNRM

EventBase *Population::getNextScheduledEvent(double &dt)
{
	// First make sure that all events have a calculated event time
	// Note that an event can be present in both the queue of a man
	// and of a woman!
	
	double curTime = getTime();

	if (!m_parallel)
	{
		for (int i = 0 ; i < m_people.size() ; i++)
			m_people[i]->processUnsortedEvents(*this, curTime);

		// TODO: can this be done in a faster way? 
		// If we still need to iterate over everyone, perhaps there's
		// really no point in trying to use an m_firstEventTracker?
	}
	else
	{
#ifndef DISABLEOPENMP
		int numPeople = m_people.size();

#ifndef DISABLE_PARALLEL
		#pragma omp parallel for 
#endif // DISABLE_PARALLEL
		for (int i = 0 ; i < numPeople ; i++)
			m_people[i]->processUnsortedEvents(*this, curTime);
#endif // !DISABLEOPENMP
	}

#ifdef STATE_SHOW_EVENTS
	showEvents();
#endif // STATE_SHOW_EVENTS

	// Then, we should look for the event that happens first
	
	PopulationEvent *pEarliestEvent = getEarliestEvent(m_people);

	// Once we've found the first event, we must remove it from the
	// relevant Person's lists

	if (pEarliestEvent == 0)
	{
		setErrorString("No event found");
		return 0;
	}

	int numPersons = pEarliestEvent->getNumberOfPersons();

	for (int i = 0 ; i < numPersons ; i++)
	{
		PersonBase *pPerson = pEarliestEvent->getPerson(i);

		assert(pPerson != 0);

		pPerson->removeTimedEvent(pEarliestEvent);
	}

	dt = pEarliestEvent->getEventTime() - getTime();

#ifndef NDEBUG
	// This is the event that's going to be fired. In debug mode we'll
	// perform a check to make sure that all the people involved directly
	// in the event are still alive
	int num = pEarliestEvent->getNumberOfPersons();
	for (int i = 0 ; i < num ; i++)
	{
		PersonBase *pPerson = pEarliestEvent->getPerson(i);

		assert(pPerson != 0);
		assert(!pPerson->hasDied());
	}
#endif // NDEBUG
	return pEarliestEvent;
}

// all affected event times should be recalculated, again note that an event pointer
// can be present in both the a man's list and a woman's list
void Population::advanceEventTimes(EventBase *pScheduledEvent, double dt)
{
	PopulationEvent *pEvt = static_cast<PopulationEvent *>(pScheduledEvent);

	// we'll schedule this event to be deleted
	
	scheduleForRemoval(pEvt);

	// the persons in this event are definitely affected

	double newRefTime = getTime() + dt;
	int numPersons = pEvt->getNumberOfPersons();

	for (int i = 0 ; i < numPersons ; i++)
	{
		PersonBase *pPerson = pEvt->getPerson(i);

		assert(pPerson != 0);

		pPerson->advanceEventTimes(*this, newRefTime);
	}

	// also get a list of other persons that are affected
	// with just the mortality event this way should suffice

	m_otherAffectedPeople.clear();
	if (POPULATION_ALWAYS_RECALCULATE_FLAG || pEvt->isEveryoneAffected())
	{
		int num = m_people.size();
		for (int i = m_numGlobalDummies ; i < num ; i++)
		{
			PersonBase *pPerson = m_people[i];

			assert(pPerson != 0);
			assert(pPerson->getGender() == PersonBase::Male || pPerson->getGender() == PersonBase::Female);

			pPerson->advanceEventTimes(*this, newRefTime);
		}
	}
	else
	{
		pEvt->markOtherAffectedPeople(*this);

		int num = m_otherAffectedPeople.size();
		for (int i = 0 ; i < num ; i++)
		{
			PersonBase *pPerson = m_otherAffectedPeople[i];

			assert(pPerson != 0);
			assert(pPerson->getGender() == PersonBase::Male || pPerson->getGender() == PersonBase::Female);

			pPerson->advanceEventTimes(*this, newRefTime);
		}
	}

	if (POPULATION_ALWAYS_RECALCULATE_FLAG || pEvt->areGlobalEventsAffected())
	{
		int num = m_numGlobalDummies;

		for (int i = 0 ; i < num ; i++)
		{
			PersonBase *pPerson = m_people[i];

			assert(pPerson != 0);
			assert(pPerson->getGender() == PersonBase::GlobalEventDummy);

			pPerson->advanceEventTimes(*this, newRefTime);
		}
	}
}
#endif // !SIMPLEMNRM

PopulationEvent *Population::getEarliestEvent(const std::vector<PersonBase *> &people)
{
	PopulationEvent *pBest = 0;
	double bestTime = -1;

	if (!m_parallel)
	{
		for (int i = 0 ; i < people.size() ; i++)
		{
			PopulationEvent *pFirstEvent = people[i]->getEarliestEvent();

			if (pFirstEvent != 0) // can happen if there are no events for this person
			{
				double t = pFirstEvent->getEventTime();

				if (pBest == 0 || t < bestTime)
				{
					bestTime = t;
					pBest = pFirstEvent;
				}
			}
		}
	}
	else
	{
#ifndef DISABLEOPENMP
		int numPeople = people.size();

		for (int i = 0 ; i < m_tmpEarliestEvents.size() ; i++)
		{
			m_tmpEarliestEvents[i] = 0;
			m_tmpEarliestTimes[i] = -1;
		}

#ifndef DISABLE_PARALLEL
		#pragma omp parallel for 
#endif // DISABLE_PARALLEL
		for (int i = 0 ; i < numPeople ; i++)
		{
			PopulationEvent *pFirstEvent = people[i]->getEarliestEvent();

			if (pFirstEvent != 0) // can happen if there are no events for this person
			{
				double t = pFirstEvent->getEventTime();
				int threadIdx = omp_get_thread_num();

				if (m_tmpEarliestEvents[threadIdx] == 0 || t < m_tmpEarliestTimes[threadIdx])
				{
					m_tmpEarliestTimes[threadIdx] = t;
					m_tmpEarliestEvents[threadIdx] = pFirstEvent;
				}
			}
		}

		for (int i = 0 ; i < m_tmpEarliestEvents.size() ; i++)
		{
			PopulationEvent *pFirstEvent = m_tmpEarliestEvents[i];

			if (pFirstEvent != 0)
			{
				double t = pFirstEvent->getEventTime();

				if (pBest == 0 || t < bestTime)
				{
					bestTime = t;
					pBest = pFirstEvent;
				}
			}

		}
#endif // !DISABLEOPENMP
	}

	return pBest;
}

void Population::scheduleForRemoval(PopulationEvent *pEvt)
{
	pEvt->setScheduledForRemoval();

#ifndef DISABLEOPENMP
	if (m_parallel)
		m_eventsToRemoveMutex.lock();
#endif // !DISABLEOPENMP

	m_eventsToRemove.push_back(pEvt);

#ifndef DISABLEOPENMP
	if (m_parallel)
		m_eventsToRemoveMutex.unlock();
#endif // !DISABLEOPENMP
}

void Population::lockEvent(PopulationEvent *pEvt) const
{
#ifndef DISABLEOPENMP
	if (!m_parallel)
		return;

	int64_t id = pEvt->getEventID();
	int64_t l = m_eventMutexes.size();

	int mutexId = (int)(id%l);

	m_eventMutexes[mutexId].lock();
#endif // !DISABLEOPENMP
}

void Population::unlockEvent(PopulationEvent *pEvt) const
{
#ifndef DISABLEOPENMP
	if (!m_parallel)
		return;

	int64_t id = pEvt->getEventID();
	int64_t l = m_eventMutexes.size();

	int mutexId = (int)(id%l);

	m_eventMutexes[mutexId].unlock();
#endif // !DISABLEOPENMP
}

void Population::lockPerson(PersonBase *pPerson) const
{
#ifndef DISABLEOPENMP
	if (!m_parallel)
		return;

	int64_t id = pPerson->getPersonID();
	int64_t l = m_personMutexes.size();

	int mutexId = (int)(id%l);

	m_personMutexes[mutexId].lock();
#endif // !DISABLEOPENMP
}

void Population::unlockPerson(PersonBase *pPerson) const
{
#ifndef DISABLEOPENMP
	if (!m_parallel)
		return;

	int64_t id = pPerson->getPersonID();
	int64_t l = m_personMutexes.size();

	int mutexId = (int)(id%l);

	m_personMutexes[mutexId].unlock();
#endif // !DISABLEOPENMP
}

#ifndef NDEBUG

void Population::markAffectedPerson(PersonBase *pPerson) const
{ 
	assert(pPerson != 0);
	assert(!pPerson->hasDied());
	m_otherAffectedPeople.push_back(pPerson); 
}

PersonBase **Population::getMen()
{
	assert(m_numMen >= 0);

	if (m_numMen == 0)
		return 0;

	PersonBase *pFirstMan = m_people[m_numGlobalDummies];
	assert(pFirstMan->getGender() == PersonBase::Male);

	return &(m_people[m_numGlobalDummies]);
}

PersonBase **Population::getWomen()
{
	assert(m_numWomen >= 0);

	if (m_numWomen == 0)
		return 0;

	int idx = m_numGlobalDummies+m_numMen;
	PersonBase *pFirstWoman = m_people[idx];

	assert(pFirstWoman->getGender() == PersonBase::Female);

	return &(m_people[idx]);
}

#endif // NDEBUG

void Population::onNewEvent(PopulationEvent *pEvt)
{
	assert(pEvt != 0);
	assert(pEvt->getEventID() < 0);

	int id = getNextEventID();
	pEvt->setEventID(id);

	assert(!pEvt->isInitialized());
	pEvt->generateNewInternalTimeDifference(getRandomNumberGenerator(), this);

	int numPersons = pEvt->getNumberOfPersons();

	if (numPersons == 0) // A global event
	{
		PersonBase *pGlobalEventPerson = m_people[0];
		assert(pGlobalEventPerson->getGender() == PersonBase::GlobalEventDummy);

		pEvt->setGlobalEventPerson(pGlobalEventPerson);
		pGlobalEventPerson->registerPersonalEvent(pEvt);
	}
	else
	{
		for (int i = 0 ; i < numPersons ; i++)
		{
			PersonBase *pPerson = pEvt->getPerson(i);

			assert(!pPerson->hasDied());

			pPerson->registerPersonalEvent(pEvt);
		}
	}

#ifdef SIMPLEMNRM
	m_allEvents.push_back(pEvt);
#endif // SIMPLEMNRM
}

#ifdef SIMPLEMNRM
void Population::onFiredEvent(EventBase *pEvt, int position)
{
	int lastEvent = m_allEvents.size()-1;

	m_allEvents[position] = m_allEvents[lastEvent];
	m_allEvents.resize(lastEvent);

	// loop over the events to see which are still valid
	// This makes things extra slow, but is is important
	// to avoid a difference in random number generator state
	// with the other version (see log Oct 24, 2013)

	int idx = 0;
	while (idx < m_allEvents.size())
	{
		PopulationEvent *pEvt = static_cast<PopulationEvent *>(m_allEvents[idx]);

		if (pEvt->isNoLongerUseful())
		{
			scheduleForRemoval(pEvt);
			
			int lastEvent = m_allEvents.size()-1;

			m_allEvents[idx] = m_allEvents[lastEvent];
			m_allEvents.resize(lastEvent);
		}
		else
			idx++;
	}
}
#endif // SIMPLEMNRM

void Population::addNewPerson(PersonBase *pPerson)
{
	assert(pPerson != 0);
	assert(pPerson->getPersonID() < 0); // should not be initialized for now
	assert(pPerson->getGender() == PersonBase::Male || pPerson->getGender() == PersonBase::Female);

	int64_t id = getNextPersonID();
	pPerson->setPersonID(id);

	if (pPerson->getGender() == PersonBase::Male) // first part of the list (but after the global event dummies)
	{
		if (m_numWomen == 0) // then it's easy
		{
			assert(m_people.size() == m_numMen+m_numGlobalDummies);

			int pos = m_people.size();
			m_people.resize(pos+1);

			m_people[pos] = pPerson;
			pPerson->setListIndex(pos);

			m_numMen++;
		}
		else // since the women are the second part of the list, we need to move the first one to the last position
		{
			PersonBase *pFirstWoman = m_people[m_numMen+m_numGlobalDummies];

			assert(pFirstWoman->getGender() == PersonBase::Female);

			int s = m_people.size();
			m_people.resize(s+1);

			m_people[s] = pFirstWoman;
			pFirstWoman->setListIndex(s);

			int newIdx = m_numMen+m_numGlobalDummies;
			m_people[newIdx] = pPerson;
			pPerson->setListIndex(newIdx);

			m_numMen++;
		}
	}
	else // Female, second part of the list
	{
		int pos = m_numGlobalDummies + m_numMen + m_numWomen;

		assert(pos == m_people.size());
		m_people.resize(pos+1);

		m_people[pos] = pPerson;
		pPerson->setListIndex(pos);

		m_numWomen++;
	}
}

#ifdef STATE_SHOW_EVENTS
#ifndef SIMPLEMNRM
void Population::showEvents()
{
	std::map<int64_t, PopulationEvent *> m;
	std::map<int64_t, PopulationEvent *>::const_iterator it;

	for (int i = 0 ; i < m_people.size() ; i++)
	{
		assert(m_people[i]->m_untimedEvents.size() == 0);

		int num = m_people[i]->m_timedEvents.size();
		for (int k = 0 ; k < num ; k++)
		{
			PopulationEvent *pEvt = m_people[i]->m_timedEvents[k];
			assert(pEvt != 0);

			int64_t id = pEvt->getEventID();

			// doesn't make sense here since an event can be present in
			// multiple people's event lists
			//assert(m.find(id) == m.end());

			m[id] = pEvt;
		}
	}

	for (it = m.begin() ; it != m.end() ; it++)
		std::cout << "   " << it->first << " -> " << it->second->getEventTime() << "," << it->second->getDescription(0) << std::endl;
}
#else // Simple version
void Population::showEvents()
{
	std::map<int64_t, PopulationEvent *> m;
	std::map<int64_t, PopulationEvent *>::const_iterator it;

	for (int i = 0 ; i < m_allEvents.size() ; i++)
	{
		PopulationEvent *pEvt = static_cast<PopulationEvent *>(m_allEvents[i]);

		assert(pEvt != 0);

		int64_t id = pEvt->getEventID();

		assert(m.find(id) == m.end());

		m[id] = pEvt;
	}

	for (it = m.begin() ; it != m.end() ; it++)
		std::cout << "   " << it->first << " -> " << it->second->getEventTime() << "," << it->second->getDescription(0) << std::endl;
}
#endif // SIMPLEMNRM
#endif // STATE_SHOW_EVENTS


