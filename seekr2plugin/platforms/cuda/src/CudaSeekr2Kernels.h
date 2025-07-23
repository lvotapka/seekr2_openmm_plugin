#ifndef CUDA_SEEKR2_KERNELS_H_
#define CUDA_SEEKR2_KERNELS_H_

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

#include "Seekr2Kernels.h"
#include "openmm/kernels.h"
#include "openmm/System.h"
#include "openmm/cuda/CudaPlatform.h"
#include "openmm/cuda/CudaContext.h"
#include "openmm/cuda/CudaArray.h"



namespace Seekr2Plugin {

/**
 * This kernel is invoked by MmvtLangevinMiddleIntegrator to take one time step.
 */
class CudaIntegrateMmvtLangevinMiddleStepKernel : public IntegrateMmvtLangevinMiddleStepKernel {
public:
    CudaIntegrateMmvtLangevinMiddleStepKernel(std::string name, const OpenMM::Platform& platform, OpenMM::CudaContext& cu); /* : IntegrateMmvtLangevinMiddleStepKernel(name, platform), cu(cu) {
    } */
    ~CudaIntegrateMmvtLangevinMiddleStepKernel();
    /**
     * Allocate memory for Cuda objects
     *
     * @param integrator     the MmvtLangevinMiddleIntegrator to obtain the info from
     */
    void allocateMemory(const MmvtLangevinMiddleIntegrator& integrator);
     /**
     * Initialize the kernel, setting up the particle masses.
     *
     * @param system     the System this kernel will be applied to
     * @param integrator the MmvtLangevinMiddleIntegrator this kernel will be used for
     */
    void initialize(const OpenMM::System& system, const MmvtLangevinMiddleIntegrator& integrator);
    /**: IntegrateMmvtLangevinMiddleStepKernel(name, platform), cu(cu)
     * Execute the kernel.
     *
     * @param context    the context in which to execute this kernel
     * @param integrator the MmvtLangevinMiddleIntegrator this kernel is being used for
     */
    void execute(OpenMM::ContextImpl& context, const MmvtLangevinMiddleIntegrator& integrator);
    /**
     * Compute the kinetic energy.
     * 
     * @param context    the context in which to execute this kernel
     * @param integrator the MmvtLangevinMiddleIntegrator this kernel is being used for
     */
    double computeKineticEnergy(OpenMM::ContextImpl& context, const MmvtLangevinMiddleIntegrator& integrator);
private:
    OpenMM::CudaContext& cu;
    double prevTemp, prevFriction, prevStepSize;
    std::vector<int> N_alpha_beta;
    std::vector<std::vector <int> > Nij_alpha;
    std::vector<double> Ri_alpha;
    double T_alpha;
    std::string outputFileName;
    OpenMM::CudaArray params;
    CUfunction kernel1, kernel2, kernel3, kernelBounce, kernelSaveOldForce;
    OpenMM::CudaArray* oldPosq;
    OpenMM::CudaArray* oldPosqCorrection;
    OpenMM::CudaArray* oldVelm;
    //OpenMM::CudaArray* oldForce;
    OpenMM::CudaArray* oldDelta;
    //std::vector<int> bitvector; // DELETE
    std::vector<int> milestoneGroups;
    bool saveStateBool = false;
    std::string saveStateFileName;
    bool saveStatisticsBool = false;
    std::string saveStatisticsFileName;
    int numMilestoneGroups, bounceCounter, previousMilestoneCrossed;
    double incubationTime;
    double firstCrossingTime;
    OpenMM::CudaArray* forcesToCheck;
};

class CudaIntegrateElberLangevinMiddleStepKernel : public IntegrateElberLangevinMiddleStepKernel {
public:
    CudaIntegrateElberLangevinMiddleStepKernel(std::string name, const OpenMM::Platform& platform, OpenMM::CudaContext& cu);
    ~CudaIntegrateElberLangevinMiddleStepKernel();
    /**
     * Allocate memory for Cuda objects
     *
     * @param integrator     the ElberLangevinMiddleIntegrator to obtain the info from
     */
    void allocateMemory(const ElberLangevinMiddleIntegrator& integrator);
     /**
     * Initialize the kernel, setting up the particle masses.
     *
     * @param system     the System this kernel will be applied to
     * @param integrator the ElberLangevinMiddleIntegrator this kernel will be used for
     */
    void initialize(const OpenMM::System& system, const ElberLangevinMiddleIntegrator& integrator);
    /**: IntegrateElberLangevinMiddleStepKernel(name, platform), cu(cu)
     * Execute the kernel.
     *
     * @param context    the context in which to execute this kernel
     * @param integrator the ElberLangevinMiddleIntegrator this kernel is being used for
     */
    void execute(OpenMM::ContextImpl& context, const ElberLangevinMiddleIntegrator& integrator);
    /**
     * Compute the kinetic energy.
     * 
     * @param context    the context in which to execute this kernel
     * @param integrator the ElberLangevinMiddleIntegrator this kernel is being used for
     */
    double computeKineticEnergy(OpenMM::ContextImpl& context, const ElberLangevinMiddleIntegrator& integrator);
private:
    OpenMM::CudaContext& cu;
    double prevTemp, prevFriction, prevStepSize;
    std::string outputFileName;
    OpenMM::CudaArray params;
    CUfunction kernel1, kernel2, kernel3;
    OpenMM::CudaArray* oldDelta;
    std::vector<int> srcbitvector;
    std::vector<int> destbitvector;
    std::vector<int> srcMilestoneGroups;
    std::vector<int> destMilestoneGroups;
    std::vector<double> srcMilestoneValues; // the force group potential energy "value" to detect crossings
    std::vector<double> destMilestoneValues;
    bool endOnSrcMilestone = true; // whether to end on one of the source milestone
    bool crossedSrcMilestone = false; // need to see if source milestone was crossed - only way to have valid statistics
    bool endSimulation = false; // If an ending milestone was crossed, then don't log any more crossings
    bool saveStateBool = false;
    std::string saveStateFileName;
    int numSrcMilestoneGroups, numDestMilestoneGroups;
    int crossingCounter;
};

} // namespace Seekr2Plugin

#endif /*CUDA_SEEKR2_KERNELS_H_*/
