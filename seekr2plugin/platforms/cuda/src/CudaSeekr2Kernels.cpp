/*
   Copyright 2019 by Lane Votapka
   All rights reserved
 * -------------------------------------------------------------------------- *
 *                                   OpenMM                                   *
 * -------------------------------------------------------------------------- *
 * This is part of the OpenMM molecular simulation toolkit originating from   *
 * Simbios, the NIH National Center for Physics-Based Simulation of           *
 * Biological Structures at Stanford, funded under the NIH Roadmap for        *
 * Medical Research, grant U54 GM072970. See https://simtk.org.               *
 *                                                                            *
 * Portions copyright (c) 2014 Stanford University and the Authors.           *
 * Authors: Peter Eastman                                                     *
 * Contributors:                                                              *
 *                                                                            *
 * Permission is hereby granted, free of charge, to any person obtaining a    *
 * copy of this software and associated documentation files (the "Software"), *
 * to deal in the Software without restriction, including without limitation  *
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,   *
 * and/or sell copies of the Software, and to permit persons to whom the      *
 * Software is furnished to do so, subject to the following conditions:       *
 *                                                                            *
 * The above copyright notice and this permission notice shall be included in *
 * all copies or substantial portions of the Software.                        *
 *                                                                            *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR *
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,   *
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL    *
 * THE AUTHORS, CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,    *
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR      *
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE  *
 * USE OR OTHER DEALINGS IN THE SOFTWARE.                                     *
 * -------------------------------------------------------------------------- */

#include "CudaSeekr2Kernels.h"
#include "CudaSeekr2KernelSources.h"
#include "openmm/cuda/CudaContext.h"
#include "openmm/internal/ContextImpl.h"
#include "openmm/cuda/CudaBondedUtilities.h"
#include "openmm/cuda/CudaForceInfo.h"
#include "openmm/cuda/CudaIntegrationUtilities.h"
#include "openmm/State.h"
#include "openmm/serialization/XmlSerializer.h"
//#include "openmm/CudaKernelSources.h"
#include "openmm/reference/SimTKOpenMMRealType.h"
#include <cmath>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <vector>

using namespace Seekr2Plugin;
using namespace OpenMM;
using namespace std;

#define CHECK_RESULT(result) \
    if (result != CUDA_SUCCESS) { \
        std::stringstream m; \
        m<<errorMessage<<": "<<cu.getErrorString(result)<<" ("<<result<<")"<<" at "<<__FILE__<<":"<<__LINE__; \
        throw OpenMMException(m.str());\
    }

CudaIntegrateMmvtLangevinMiddleStepKernel::CudaIntegrateMmvtLangevinMiddleStepKernel(
                                    std::string name, 
                                    const OpenMM::Platform& platform, 
                                    OpenMM::CudaContext& cu) : 
                                    IntegrateMmvtLangevinMiddleStepKernel(name, 
                                    platform), cu(cu) {
    oldPosq = nullptr;
    oldPosqCorrection = nullptr;
    oldVelm = nullptr;
    oldDelta = nullptr;
}

CudaIntegrateMmvtLangevinMiddleStepKernel::~CudaIntegrateMmvtLangevinMiddleStepKernel() {
    cu.setAsCurrent();
    delete oldPosq;
    delete oldPosqCorrection;
    delete oldVelm;
    delete oldDelta;
}

void CudaIntegrateMmvtLangevinMiddleStepKernel::allocateMemory(const MmvtLangevinMiddleIntegrator& integrator) {
    int numAtoms = cu.getNumAtoms();
    int paddedNumAtoms = cu.getPaddedNumAtoms();
    
    if (cu.getUseDoublePrecision()) {
        oldPosq           = CudaArray::create<double4> (cu, numAtoms, "oldPosq");
        oldPosqCorrection = CudaArray::create<double4> (cu, numAtoms, "oldPosqCorrection");
        oldVelm           = CudaArray::create<double4> (cu, numAtoms, "oldVelm");
        oldDelta          = CudaArray::create<double4> (cu, paddedNumAtoms, "oldDelta");
    } else if (cu.getUseMixedPrecision()) {
        oldPosq           = CudaArray::create<float4> (cu, numAtoms, "oldPosq");
        oldPosqCorrection = CudaArray::create<float4> (cu, numAtoms, "oldPosqCorrection");
        oldVelm           = CudaArray::create<double4> (cu, numAtoms, "oldVelm");
        oldDelta          = CudaArray::create<double4> (cu, paddedNumAtoms, "oldDelta");
    } else {
        oldPosq           = CudaArray::create<float4> (cu, numAtoms, "oldPosq");
        oldPosqCorrection = CudaArray::create<float4> (cu, numAtoms, "oldPosqCorrection");
        oldVelm           = CudaArray::create<float4> (cu, numAtoms, "oldVelm");
        oldDelta          = CudaArray::create<float4> (cu, paddedNumAtoms, "oldDelta");
    }
}

void CudaIntegrateMmvtLangevinMiddleStepKernel::initialize(const System& system, const MmvtLangevinMiddleIntegrator& integrator) {
    bool output_file_already_exists;
    cu.getPlatformData().initializeContexts(system);
    cu.setAsCurrent();
    allocateMemory(integrator);
    cu.getIntegrationUtilities().initRandomNumberGenerator(integrator.getRandomNumberSeed());
    map<string, string> defines;
    CUmodule module = cu.createModule(CudaSeekr2KernelSources::langevinMiddle, defines, "");
    kernel1 = cu.getKernel(module, "integrateMmvtLangevinMiddlePart1");
    kernel2 = cu.getKernel(module, "integrateMmvtLangevinMiddlePart2");
    kernel3 = cu.getKernel(module, "integrateMmvtLangevinMiddlePart3");
    kernelBounce = cu.getKernel(module, "mmvtBounce");
    //params.initialize(cu, 3, cu.getUseDoublePrecision() || cu.getUseMixedPrecision() ? sizeof(double) : sizeof(float), "mmvtLangevinMiddleParams");
    if (cu.getUseDoublePrecision() || cu.getUseMixedPrecision()) {
        params.initialize<double>(cu, 2, "mmvtLangevinMiddleParams");
        //oldDelta.initialize<mm_double4>(cu, cu.getPaddedNumAtoms(), "oldDelta");
    }
    else {
        params.initialize<float>(cu, 2, "mmvtLangevinMiddleParams");
        //oldDelta.initialize<mm_float4>(cu, cu.getPaddedNumAtoms(), "oldDelta");
    }
    prevStepSize = -1.0;
    
    N_alpha_beta = vector<int> (integrator.getNumMilestoneGroups());
    for (int i=0; i<integrator.getNumMilestoneGroups(); i++) {
        N_alpha_beta[i] = 0;
    }
    Nij_alpha = vector<vector <int> > (integrator.getNumMilestoneGroups());
    for (int i=0; i<integrator.getNumMilestoneGroups(); i++) {
        Nij_alpha[i] = vector<int>(integrator.getNumMilestoneGroups());
        for (int j=0; j<integrator.getNumMilestoneGroups(); j++) {
            Nij_alpha[i][j] = 0;
        }
    }
    Ri_alpha = vector<double> (integrator.getNumMilestoneGroups());
    for (int i=0; i<integrator.getNumMilestoneGroups(); i++) {
        Ri_alpha[i] = 0.0;
    }
    T_alpha = 0.0;
    //New parameters for running integrator
    outputFileName = integrator.getOutputFileName();
    saveStateFileName = integrator.getSaveStateFileName();
    if (saveStateFileName.empty()) {
        saveStateBool = false;
    } else {
        saveStateBool = true;
    }
    saveStatisticsFileName = integrator.getSaveStatisticsFileName();
    if (saveStatisticsFileName.empty()) {
        saveStatisticsBool = false;
    } else {
        saveStatisticsBool = true;
    }
    for (int i=0; i<integrator.getNumMilestoneGroups(); i++) {
        milestoneGroups.push_back(integrator.getMilestoneGroup(i));
    }
    bounceCounter = integrator.getBounceCounter();
    incubationTime = 0.0;
    firstCrossingTime = 0.0;
    previousMilestoneCrossed = -1;
    assert(cu.getStepCount() == 0);
    assert(cu.getTime() == 0.0);
    // see whether the file already exists
    ifstream datafile;
    datafile.open(outputFileName);
    if (datafile) {
        output_file_already_exists = true;
    } else {
        output_file_already_exists = false;
    }
    datafile.close();
    if (output_file_already_exists == false) {
        ofstream datafile; // open datafile for writing
        datafile.open(outputFileName, std::ios_base::app); // write new file
        datafile << "#\"Bounced boundary ID\",\"bounce index\",\"total time (ps)\"\n";
        datafile.close(); // close data file
    }
}

void CudaIntegrateMmvtLangevinMiddleStepKernel::execute(ContextImpl& context, const MmvtLangevinMiddleIntegrator& integrator) {
    cu.setAsCurrent();
    CudaIntegrationUtilities& integration = cu.getIntegrationUtilities();
    int numAtoms = cu.getNumAtoms();
    int paddedNumAtoms = cu.getPaddedNumAtoms();
    double temperature = integrator.getTemperature();
    double friction = integrator.getFriction();
    double stepSize = integrator.getStepSize();
    float value = 0.0;
    bool includeForces = false;
    bool includeEnergy = true;
    
    cu.getIntegrationUtilities().setNextStepSize(stepSize);
    if (temperature != prevTemp || friction != prevFriction || stepSize != prevStepSize) {
        // Calculate the integration parameters.

        double kT = BOLTZ*temperature;
        double vscale = exp(-stepSize*friction);
        double noisescale = sqrt(kT*(1-vscale*vscale));
        vector<double> p(params.getSize());
        p[0] = vscale;
        p[1] = noisescale;
        params.upload(p, true);
        prevTemp = temperature;
        prevFriction = friction;
        prevStepSize = stepSize;
    }
    
    if (cu.getStepCount() <= 0) {
        value = context.calcForcesAndEnergy(includeForces, includeEnergy, 2);
        if (value > 0.0) { // take a step back and reverse velocities
            throw OpenMMException("MMVT simulation bouncing on first step: the system will be trapped behind a boundary. Check and revise MMVT boundary definitions and/or atomic positions.");
        }
    }
    
    // Call the first integration kernel.
    
    int randomIndex = integration.prepareRandomNumbers(cu.getPaddedNumAtoms());
    void* args1[] = {&numAtoms, &paddedNumAtoms, 
            &cu.getVelm().getDevicePointer(), 
            &cu.getForce().getDevicePointer(),
            &integration.getStepSize().getDevicePointer()};
    cu.executeKernel(kernel1, args1, numAtoms, 128);
    
    // Apply velocity constraints.

    integration.applyVelocityConstraints(integrator.getConstraintTolerance());
    
    // Call the second integration kernel.

    void* args2[] = {&numAtoms, &cu.getVelm().getDevicePointer(),
            &integration.getPosDelta().getDevicePointer(),
            &oldDelta->getDevicePointer(), 
            &params.getDevicePointer(), 
            &integration.getStepSize().getDevicePointer(), 
            &oldVelm->getDevicePointer(),
            &integration.getRandom().getDevicePointer(), 
            &randomIndex};
    cu.executeKernel(kernel2, args2, numAtoms, 128);
    
    // Apply constraints.

    integration.applyConstraints(integrator.getConstraintTolerance());

    // Call the third integration kernel.

    CUdeviceptr posCorrection = (cu.getUseMixedPrecision() ? cu.getPosqCorrection().getDevicePointer() : 0);
    void* args3[] = {&numAtoms, &cu.getPosq().getDevicePointer(), 
            &cu.getVelm().getDevicePointer(), 
            &integration.getPosDelta().getDevicePointer(),
            &oldDelta->getDevicePointer(), 
            &integration.getStepSize().getDevicePointer(),
            &oldPosq->getDevicePointer(), &posCorrection};
    cu.executeKernel(kernel3, args3, numAtoms, 128);
    integration.computeVirtualSites();
    
    // Monitor for one or more milestone crossings
    int bitcode;
    bool bounced = false;
    int num_bounced_surfaces = 0;
    
    //value = context.calcForcesAndEnergy(includeForces, includeEnergy, bitvector[i]);
    value = context.calcForcesAndEnergy(includeForces, includeEnergy, 2);
    if (value > 0.0) { // take a step back and reverse velocities
        bounced = true;
        // Write to output file
        ofstream datafile; // open datafile for writing
        datafile.open(outputFileName, std::ios_base::app); // append to file
        datafile.setf(std::ios::fixed,std::ios::floatfield);
        datafile.precision(3);
        bitcode = static_cast<int>(value);
        // check for corner bounce so as not to save state
        for (int i=0; i<milestoneGroups.size(); i++) {
            if ((bitcode % 2) != 0) {
                // count the number of bounced surfaces
                num_bounced_surfaces++;
            }
            bitcode = bitcode >> 1;
        }
        bitcode = static_cast<int>(value);
        for (int i=0; i<integrator.getNumMilestoneGroups(); i++) {
            if ((bitcode % 2) == 0) {
                // if value is not showing this as crossed, continue
                bitcode = bitcode >> 1;
                continue;
            }
            bitcode = bitcode >> 1;
            
            datafile << milestoneGroups[i] << "," << bounceCounter << "," << context.getTime() << "\n";
            if (saveStateBool == true && num_bounced_surfaces == 1) {
                State myState = context.getOwner().getState(State::Positions | State::Velocities);
                stringstream buffer;
                stringstream number_str;
                number_str << "_" << bounceCounter << "_" << milestoneGroups[i];
                string trueFileName = saveStateFileName + number_str.str();
                XmlSerializer::serialize<State>(&myState, "State", buffer);
                ofstream statefile; // open datafile for writing
                statefile.open(trueFileName, std::ios_base::trunc); // append to file
                statefile << buffer.rdbuf();
                statefile.close(); // close data file
            }
            if (previousMilestoneCrossed != -1) {
                N_alpha_beta[i] += 1;
            }
            if (previousMilestoneCrossed != i) {
                if (previousMilestoneCrossed != -1) { // if this isn't the first time a bounce has occurred
                    Nij_alpha[previousMilestoneCrossed][i] += 1; 
                    Ri_alpha[previousMilestoneCrossed] += incubationTime;
                } else {
                    firstCrossingTime = cu.getTime();
                }
                incubationTime = 0.0;
            }
            T_alpha = cu.getTime() - firstCrossingTime;
            
            previousMilestoneCrossed = i;
            bounceCounter++;
        }
        datafile.close(); // close data file
        if (saveStatisticsBool == true) {
            //throw OpenMMException("Statistics file feature not working: saveStatisticsBool must be set to 'false' at this time");
            /* // TODO: remove
            // This feature is temporarily disabled. With the improvement
            // of moving all boundary definitions to a single force group,
            // this feature is now broken. Since it is not actively used
            // yet, we will postpone its functionality. To get this 
            // working, this plugin would need to be provided with an
            // array of milestone indices, similar to the function that
            // milestoneGroups used to have (but now, milestoneGroups
            // will merely be an array of size 1).
            */
            ofstream stats;
            stats.open(saveStatisticsFileName, ios_base::trunc);
            stats.setf(std::ios::fixed,std::ios::floatfield);
            stats.precision(3);
            for (int i = 0; i < N_alpha_beta.size(); i++) {
                stats << "N_alpha_" << milestoneGroups[i] << ": " << N_alpha_beta[i] << "\n";
            }
            int nij_index = 0;
            for (int i = 0; i < integrator.getNumMilestoneGroups(); i++) {
                for (int j = 0; j < integrator.getNumMilestoneGroups(); j++) {
                    stats << "N_" << milestoneGroups[i] << "_" << milestoneGroups[j] << "_alpha: " << Nij_alpha[i][j] << "\n"; 
                    nij_index += 1;
                }
            }
            for (int i = 0; i < integrator.getNumMilestoneGroups(); i++) {
                stats << "R_" << milestoneGroups[i] << "_alpha: " << Ri_alpha[i] << "\n";       
            }
            stats << "T_alpha: " << T_alpha << "\n";  
            stats.close();
            
        }
    }
    
    // If one or more milestone boundaries were crossed, perform bounce
    
    if (bounced == true) {
        void* argsBounce[] = {&numAtoms, &paddedNumAtoms, 
            &cu.getPosq().getDevicePointer(), 
            &cu.getVelm().getDevicePointer(), 
            &oldPosq->getDevicePointer(), 
            &oldVelm->getDevicePointer()}; //,
        cu.executeKernel(kernelBounce, argsBounce, numAtoms, 128);
    }
    
    // Update the time and step count.
    cu.setTime(cu.getTime()+stepSize);
    cu.setStepCount(cu.getStepCount()+1);
    incubationTime += stepSize;
    cu.reorderAtoms();
}

double CudaIntegrateMmvtLangevinMiddleStepKernel::computeKineticEnergy(ContextImpl& context, const MmvtLangevinMiddleIntegrator& integrator) {
    return cu.getIntegrationUtilities().computeKineticEnergy(0.5*integrator.getStepSize());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CudaIntegrateElberLangevinMiddleStepKernel::CudaIntegrateElberLangevinMiddleStepKernel(std::string name, 
                                    const OpenMM::Platform& platform, 
                                    OpenMM::CudaContext& cu) : 
                                    IntegrateElberLangevinMiddleStepKernel(name, 
                                    platform), cu(cu) {
    
}

CudaIntegrateElberLangevinMiddleStepKernel::~CudaIntegrateElberLangevinMiddleStepKernel() {
    cu.setAsCurrent();
}

void CudaIntegrateElberLangevinMiddleStepKernel::allocateMemory(const ElberLangevinMiddleIntegrator& integrator) {
    int numAtoms = cu.getNumAtoms();
    int paddedNumAtoms = cu.getPaddedNumAtoms();
    
    if (cu.getUseDoublePrecision()) {
        oldDelta          = CudaArray::create<double4> (cu, paddedNumAtoms, "oldDelta");
    } else if (cu.getUseMixedPrecision()) {
        oldDelta          = CudaArray::create<double4> (cu, paddedNumAtoms, "oldDelta");
    } else {
        oldDelta          = CudaArray::create<float4> (cu, paddedNumAtoms, "oldDelta");
    }
}

void CudaIntegrateElberLangevinMiddleStepKernel::initialize(const System& system, const ElberLangevinMiddleIntegrator& integrator) {
    bool output_file_already_exists;
    cu.getPlatformData().initializeContexts(system);
    cu.setAsCurrent();
    allocateMemory(integrator);
    cu.getIntegrationUtilities().initRandomNumberGenerator(integrator.getRandomNumberSeed());
    map<string, string> defines;
    CUmodule module = cu.createModule(CudaSeekr2KernelSources::langevinMiddle, defines, "");
    kernel1 = cu.getKernel(module, "integrateElberLangevinMiddlePart1");
    kernel2 = cu.getKernel(module, "integrateElberLangevinMiddlePart2");
    kernel3 = cu.getKernel(module, "integrateElberLangevinMiddlePart3");
    //params.initialize(cu, 3, cu.getUseDoublePrecision() || cu.getUseMixedPrecision() ? sizeof(double) : sizeof(float), "mmvtLangevinMiddleParams");
    if (cu.getUseDoublePrecision() || cu.getUseMixedPrecision()) {
        params.initialize<double>(cu, 2, "elberLangevinMiddleParams");
        //oldDelta.initialize<mm_double4>(cu, cu.getPaddedNumAtoms(), "oldDelta");
    }
    else {
        params.initialize<float>(cu, 2, "elberLangevinMiddleParams");
        //oldDelta.initialize<mm_float4>(cu, cu.getPaddedNumAtoms(), "oldDelta");
    }
    prevStepSize = -1.0;
    crossingCounter = integrator.getCrossingCounter();
    crossedSrcMilestone = false;
    
    //New parameters for running integrator
    outputFileName = integrator.getOutputFileName();
    saveStateFileName = integrator.getSaveStateFileName();
    if (saveStateFileName.empty()) {
        saveStateBool = false;
    } else {
        saveStateBool = true;
    }
    endOnSrcMilestone = integrator.getEndOnSrcMilestone();
    for (int i=0; i<integrator.getNumSrcMilestoneGroups(); i++) {
        bool foundForceGroup=false;
        for (int j=0; j<system.getNumForces(); j++) {
            if (system.getForce(j).getForceGroup() == integrator.getSrcMilestoneGroup(i))
                foundForceGroup=true;
        }
        if (foundForceGroup == false)
            throw OpenMMException("System contains no force groups used to detect Elber boundary crossings. Check for mismatches between force group assignments and the groups added to the Elber integrator.");
        srcMilestoneGroups.push_back(integrator.getSrcMilestoneGroup(i));
        srcMilestoneValues.push_back(-INFINITY);
    }
    for (int i=0; i<integrator.getNumDestMilestoneGroups(); i++) {
        bool foundForceGroup=false;
        for (int j=0; j<system.getNumForces(); j++) {
            if (system.getForce(j).getForceGroup() == integrator.getDestMilestoneGroup(i))
                foundForceGroup=true;
        }
        if (foundForceGroup == false)
            throw OpenMMException("System contains no force groups used to detect Elber boundary crossings. Check for mismatches between force group assignments and the groups added to the Elber integrator.");
        destMilestoneGroups.push_back(integrator.getDestMilestoneGroup(i));
        destMilestoneValues.push_back(-INFINITY);
    }
    srcbitvector.clear();
    for (int i=0; i<integrator.getNumSrcMilestoneGroups(); i++) {
        srcbitvector.push_back(pow(2,integrator.getSrcMilestoneGroup(i)));
    }
    destbitvector.clear();
    for (int i=0; i<integrator.getNumDestMilestoneGroups(); i++) {
        destbitvector.push_back(pow(2,integrator.getDestMilestoneGroup(i)));
    }
    assert(cu.getStepCount() == 0);
    assert(cu.getTime() == 0.0);
    // see whether the file already exists
    ifstream datafile;
    datafile.open(outputFileName);
    if (datafile) {
        output_file_already_exists = true;
    } else {
        output_file_already_exists = false;
    }
    datafile.close();
    if (output_file_already_exists == false) {
        ofstream datafile; // open datafile for writing
        datafile.open(outputFileName, std::ios_base::app); // write new file
        datafile << "#\"Crossed boundary ID\",\"crossing counter\",\"total time (ps)\"\n";
        datafile << "# An asterisk(*) indicates that source milestone was never crossed - asterisked statistics are invalid and should be excluded.\n";
        datafile.close(); // close data file
    }
}

void CudaIntegrateElberLangevinMiddleStepKernel::execute(ContextImpl& context, const ElberLangevinMiddleIntegrator& integrator) {
    cu.setAsCurrent();
    CudaIntegrationUtilities& integration = cu.getIntegrationUtilities();
    int numAtoms = cu.getNumAtoms();
    int paddedNumAtoms = cu.getPaddedNumAtoms();
    double temperature = integrator.getTemperature();
    double friction = integrator.getFriction();
    double stepSize = integrator.getStepSize();
    cu.getIntegrationUtilities().setNextStepSize(stepSize);
    if (temperature != prevTemp || friction != prevFriction || stepSize != prevStepSize) {
        // Calculate the integration parameters.

        double kT = BOLTZ*temperature;
        double vscale = exp(-stepSize*friction);
        double noisescale = sqrt(kT*(1-vscale*vscale));
        vector<double> p(params.getSize());
        p[0] = vscale;
        p[1] = noisescale;
        params.upload(p, true);
        prevTemp = temperature;
        prevFriction = friction;
        prevStepSize = stepSize;
    }
    // Call the first integration kernel.

    int randomIndex = integration.prepareRandomNumbers(cu.getPaddedNumAtoms());
    void* args1[] = {&numAtoms, &paddedNumAtoms, 
            &cu.getVelm().getDevicePointer(), 
            &cu.getForce().getDevicePointer(),
            &integration.getStepSize().getDevicePointer()};
    cu.executeKernel(kernel1, args1, numAtoms, 128);
    
    // Apply velocity constraints.

    integration.applyVelocityConstraints(integrator.getConstraintTolerance());
    
    // Call the second integration kernel.

    void* args2[] = {&numAtoms, &cu.getVelm().getDevicePointer(),
            &integration.getPosDelta().getDevicePointer(),
            &oldDelta->getDevicePointer(), 
            &params.getDevicePointer(), 
            &integration.getStepSize().getDevicePointer(), 
            &integration.getRandom().getDevicePointer(), 
            &randomIndex};
    cu.executeKernel(kernel2, args2, numAtoms, 128);
    
    // Apply constraints.

    integration.applyConstraints(integrator.getConstraintTolerance());

    // Call the second integration kernel.

    CUdeviceptr posCorrection = (cu.getUseMixedPrecision() ? cu.getPosqCorrection().getDevicePointer() : 0);
    void* args3[] = {&numAtoms, &cu.getPosq().getDevicePointer(), 
            &cu.getVelm().getDevicePointer(), 
            &integration.getPosDelta().getDevicePointer(),
            &oldDelta->getDevicePointer(), 
            &integration.getStepSize().getDevicePointer(),
            &posCorrection};
    cu.executeKernel(kernel3, args3, numAtoms, 128);
    integration.computeVirtualSites();
    
    // Monitor for one or more milestone crossings
    float value = 0.0;
    float oldvalue = 0.0;
    bool includeForces = false;
    bool includeEnergy = true;
    int endMilestoneGroup = 0;
    int num_bounced_surfaces = 0;
    if (endSimulation == false) {
        // first check source milestone crossings
        for (int i=0; i<integrator.getNumSrcMilestoneGroups(); i++) {
            value = context.calcForcesAndEnergy(includeForces, includeEnergy, srcbitvector[i]);
            if (srcMilestoneValues[i] == -INFINITY) {
                // First timestep
                srcMilestoneValues[i] = value;
            }
            oldvalue = srcMilestoneValues[i];
            if (((value - oldvalue) != 0.0) && (cu.getTime() != 0.0)) {
                // The source milestone has been crossed
                if (endOnSrcMilestone == true) {
                    endSimulation = true;
                    num_bounced_surfaces++;
                    ofstream datafile;
                    datafile.open(outputFileName, std::ios_base::app);
                    datafile << integrator.getSrcMilestoneGroup(i) << "," << crossingCounter << "," << context.getTime() << "\n";
                    endMilestoneGroup = integrator.getSrcMilestoneGroup(i);
                    datafile.close();
                } else {
                    crossedSrcMilestone = true;
                    context.setTime(0.0); // reset the timer
                    srcMilestoneValues[i] = value;
                }
            } 
        }
        // then check destination milestone crossings
        for (int i=0; i<integrator.getNumDestMilestoneGroups(); i++) {
            value = context.calcForcesAndEnergy(includeForces, includeEnergy, destbitvector[i]);
            if (destMilestoneValues[i] == -INFINITY) {
                // First timestep
                destMilestoneValues[i] = value;
            }
            oldvalue = destMilestoneValues[i];
            if (((value - oldvalue) != 0.0) && (cu.getTime() != 0.0)) {
                // The destination milestone has been crossed
                endSimulation = true;
                num_bounced_surfaces++;
                ofstream datafile;
                datafile.open(outputFileName, std::ios_base::app);
                if ((crossedSrcMilestone == true) || (endOnSrcMilestone == true)) {
                    datafile << integrator.getDestMilestoneGroup(i) << "," << crossingCounter << "," << context.getTime() << "\n";
                } else {
                    datafile << integrator.getDestMilestoneGroup(i) << "*," << crossingCounter << "," << context.getTime() << "\n";
                }
                endMilestoneGroup = integrator.getDestMilestoneGroup(i);
                datafile.close();
            } 
        }
        if (endSimulation == true) {
            // Then a crossing event has just occurred.
            if (saveStateBool == true && num_bounced_surfaces == 1) {
                State myState = context.getOwner().getState(State::Positions | State::Velocities);
                stringstream buffer;
                stringstream number_str;
                number_str << "_" << endMilestoneGroup;
                string trueFileName = saveStateFileName + number_str.str();
                XmlSerializer::serialize<State>(&myState, "State", buffer);
                ofstream statefile; // open datafile for writing
                statefile.open(trueFileName, std::ios_base::trunc); // append to file
                statefile << buffer.rdbuf();
                statefile.close(); // close data file
            }
            crossingCounter ++;
        }
    }
    
    // Update timesteps
    cu.setTime(cu.getTime()+stepSize);
    cu.setStepCount(cu.getStepCount()+1);
    cu.reorderAtoms();
}

double CudaIntegrateElberLangevinMiddleStepKernel::computeKineticEnergy(ContextImpl& context, const ElberLangevinMiddleIntegrator& integrator) {
    return cu.getIntegrationUtilities().computeKineticEnergy(0.5*integrator.getStepSize());
}
