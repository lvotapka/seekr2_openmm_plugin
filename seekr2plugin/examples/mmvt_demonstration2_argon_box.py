from simtk.openmm.app import *
from simtk.openmm import *
from simtk.unit import *
from sys import stdout
from seekr2plugin import MmvtLangevinMiddleIntegrator, vectori, vectord
import seekr2plugin
from time import time
import numpy as np
import os

def writePdbFrame(filename, frameNum, state):
    posInNm = state.getPositions()
    myfile = open(filename, 'a')
    # Use PDB MODEL cards to number trajectory frames
    myfile.write("MODEL     %d\n" % frameNum) # start of frame
    #for (int a = 0; a < (int)posInNm.size(); ++a)
    for a in range(0, len(posInNm)):
        myfile.write("ATOM  %5d  AR   AR     1    " % (a+1)) # atom number
        myfile.write("%8.3f%8.3f%8.3f  1.00  0.00\n" % (posInNm[a][0].value_in_unit(angstroms), posInNm[a][1].value_in_unit(angstroms), posInNm[a][2].value_in_unit(angstroms)))
        
    myfile.write("ENDMDL\n") # end of frame
    myfile.close()


filename = "argon_box_out.pdb"
box_edge = 25 * angstroms
nparticles = 10
mass = 39.9 * amu # mass
sigma = 3.4 * angstrom # Lennard-Jones sigma
epsilon = 0.238 * kilocalories_per_mole # Lennard-Jones well-depth
charge = 0.0 * elementary_charge # argon model has no charge

cutoff = 2.5*sigma # Compute cutoff
#pdb = PDBFile('argons.pdb')
#forcefield = ForceField('amber14-all.xml', 'argon.xml')
#system = forcefield.createSystem(pdb.topology, nonbondedMethod=PME, nonbondedCutoff=1*nanometer, constraints=HBonds)
system = System()
system.setDefaultPeriodicBoxVectors(np.array([box_edge, 0, 0]), np.array([0, box_edge, 0]), np.array([0, 0, box_edge]))
force = CustomNonbondedForce("""4*epsilon*(1/(((r/sigma)^6)^2) - 1/((r/sigma)^6));
                                   sigma=0.5*(sigma1+sigma2);
                                   epsilon=sqrt(epsilon1*epsilon2);
                                   """)
force.addPerParticleParameter("sigma")
force.addPerParticleParameter("epsilon")
force.setNonbondedMethod(NonbondedForce.CutoffPeriodic)
force.setCutoffDistance(cutoff) 
for particle_index in range(nparticles):
    system.addParticle(mass)
    force.addParticle([sigma, epsilon])

system.addForce(force)


positions = Quantity(numpy.random.uniform(high=box_edge/angstroms, size=[nparticles,3]), angstrom)

positions[0][0] = 12 * angstrom
positions[0][1] = 12 * angstrom
positions[0][2] = 12 * angstrom


myforce1 = CustomCentroidBondForce(1, "k*(x1-bound)")
mygroup1 = myforce1.addGroup([0]) # atom index 0
myforce1.setForceGroup(1)
myforce1.addPerBondParameter('k')
myforce1.addPerBondParameter('bound')
myforce1.addBond([mygroup1], [1.0e-9*kilojoules_per_mole, 20.0*angstroms])
forcenum = system.addForce(myforce1)

myforce2 = CustomCentroidBondForce(1, "-k*(x1-bound)")
mygroup2 = myforce2.addGroup([0]) # atom index 0
myforce2.setForceGroup(2)
myforce2.addPerBondParameter('k')
myforce2.addPerBondParameter('bound')
myforce2.addBond([mygroup2], [1.0e-9*kilojoules_per_mole, 4.0*angstroms])
forcenum = system.addForce(myforce2)

myforce3 = CustomCentroidBondForce(1, "k*(y1-bound)")
mygroup3 = myforce3.addGroup([0]) # atom index 0
myforce3.setForceGroup(3)
myforce3.addPerBondParameter('k')
myforce3.addPerBondParameter('bound')
myforce3.addBond([mygroup3], [1.0e-9*kilojoules_per_mole, 20.0*angstroms])
forcenum = system.addForce(myforce3)

myforce4 = CustomCentroidBondForce(1, "-k*(y1-bound)")
mygroup4 = myforce4.addGroup([0]) # atom index 0
myforce4.setForceGroup(4)
myforce4.addPerBondParameter('k')
myforce4.addPerBondParameter('bound')
myforce4.addBond([mygroup4], [1.0e-9*kilojoules_per_mole, 4.0*angstroms])
forcenum = system.addForce(myforce4)

myforce5 = CustomCentroidBondForce(1, "k*(z1-bound)")
mygroup5 = myforce5.addGroup([0]) # atom index 0
myforce5.setForceGroup(5)
myforce5.addPerBondParameter('k')
myforce5.addPerBondParameter('bound')
myforce5.addBond([mygroup5], [1.0e-9*kilojoules_per_mole, 20.0*angstroms])
forcenum = system.addForce(myforce5)

myforce6 = CustomCentroidBondForce(1, "-k*(z1-bound)")
mygroup6 = myforce6.addGroup([0]) # atom index 0
myforce6.setForceGroup(6)
myforce6.addPerBondParameter('k')
myforce6.addPerBondParameter('bound')
myforce6.addBond([mygroup6], [1.0e-9*kilojoules_per_mole, 4.0*angstroms])
forcenum = system.addForce(myforce6)


integrator = MmvtLangevinMiddleIntegrator(300*kelvin, 1/picosecond, 0.002*picoseconds, "test_filename.txt")
integrator.addMilestoneGroup(1)
integrator.addMilestoneGroup(2)
integrator.addMilestoneGroup(3)
integrator.addMilestoneGroup(4)
integrator.addMilestoneGroup(5)
integrator.addMilestoneGroup(6)

platform = Platform.getPlatformByName('CUDA')
properties = {'CudaDeviceIndex': '0', 'CudaPrecision': 'mixed'} # CUDA platform

context = Context(system, integrator)
context.setPositions(positions)
context.setVelocitiesToTemperature(300*kelvin)
LocalEnergyMinimizer.minimize(context)

if os.path.exists(filename):
    os.system('rm %s' % filename) # delete the existing transition data file if it exists

for iteration in range(3000):
    integrator.step(10)
    state = context.getState(getPositions=True)
    positions = state.getPositions(asNumpy=True)
    writePdbFrame(filename, iteration, state)


'''
simulation = Simulation(pdb.topology, system, integrator, platform, properties) 

#simulation = Simulation(pdb.topology, system, integrator) # Reference platform
simulation.context.setPositions(positions)
simulation.context.setVelocitiesToTemperature(300*kelvin)
simulation.minimizeEnergy()
simulation.reporters.append(PDBReporter('demonstration1_argon_sphere.pdb', 10))
simulation.reporters.append(StateDataReporter(stdout, 100, step=True, potentialEnergy=True, temperature=True))
starttime = time()
simulation.step(1000)
print "time:", time() - starttime

'''
