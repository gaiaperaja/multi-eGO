import itertools
import pandas as pd
from read_input import read_top
import numpy as np

# Import the topology informations
topology = read_top()
protein = topology.molecules[0]
exclusion_list_gromologist = []

# To import the [ atoms ] section of the topology
atom_topology_num, atom_topology_type, atom_topology_resid, atom_topology_resname, atom_topology_name, atom_topology_mass  = protein.list_atoms()
raw_topology_atoms = pd.DataFrame(np.column_stack([atom_topology_num, atom_topology_type, atom_topology_resid, atom_topology_resname, atom_topology_name, atom_topology_mass]), columns=['nr', 'type','resnr', 'residue', 'atom', 'mass'])

# Making a list for left-alpha corrections
#left_alpha = raw_topology_atoms

# Changing the mass of the atoms section by adding the H
raw_topology_atoms['mass'].astype(float)
mask = ((raw_topology_atoms['type'] == 'N') | (raw_topology_atoms['type'] == 'OA')) | ((raw_topology_atoms['residue'] == 'TYR') & ((raw_topology_atoms['atom'] == 'CD1') | (raw_topology_atoms['atom'] == 'CD2') | (raw_topology_atoms['atom'] == 'CE1') | (raw_topology_atoms['atom'] == 'CE2')))
raw_topology_atoms['mass'][mask] = raw_topology_atoms['mass'][mask].astype(float).add(1)

# Same thing here with the N terminal which is charged
mask = (raw_topology_atoms['resnr'] == '1') & (raw_topology_atoms['atom'] == 'N')
raw_topology_atoms['mass'][mask] = raw_topology_atoms['mass'][mask].astype(float).add(2)

# Structure based atomtype definition
raw_topology_atoms['sb_type'] = raw_topology_atoms['atom'] + '_' + raw_topology_atoms['resnr']

# This is needed when we want to do some stuff only to the N terminal
first_resid = 'N_'+str(atom_topology_resid[0])

# ACID pH
# Selection of the aminoacids and the charged atoms (used for B2m)
# TODO add some options for precise pH setting
acid_ASP = raw_topology_atoms[(raw_topology_atoms['residue'] == "ASP") & ((raw_topology_atoms['atom'] == "OD1") | (raw_topology_atoms['atom'] == "OD2") | (raw_topology_atoms['atom'] == "CG"))]
acid_GLU = raw_topology_atoms[(raw_topology_atoms['residue'] == "GLU") & ((raw_topology_atoms['atom'] == "OE1") | (raw_topology_atoms['atom'] == "OE2") | (raw_topology_atoms['atom'] == "CD"))]
acid_HIS = raw_topology_atoms[(raw_topology_atoms['residue'] == "HIS") & ((raw_topology_atoms['atom'] == "ND1") | (raw_topology_atoms['atom'] == "CE1") | (raw_topology_atoms['atom'] == "NE2") | (raw_topology_atoms['atom'] == "CD2") | (raw_topology_atoms['atom'] == "CG"))]
frames = [acid_ASP, acid_GLU, acid_HIS]
acid_atp = pd.concat(frames, ignore_index = True)
acid_atp = acid_atp['sb_type'].tolist()

# Harp 0
gromos_atp = pd.DataFrame(
    {'name': ['O', 'OA', 'N', 'C', 'CH1', 
            'CH2', 'CH3', 'CH2r', 'NT', 'S',
            'NR', 'OM', 'NE', 'NL', 'NZ'],
     'mass': [16, 17, 15, 12, 13, 14, 15, 14, 17, 32, 14, 16, 15, 17, 16],
     'at.num': [8, 8, 7, 6, 6, 6, 6, 6, 7, 16, 7, 8, 7, 7, 7],
     'c12': [1e-06, 1.505529e-06, 2.319529e-06, 4.937284e-06, 9.70225e-05, # CH1
            3.3965584e-05, 2.6646244e-05, 2.8058209e-05, 5.0625e-06, 1.3075456e-05,
            3.389281e-06, 7.4149321e-07, 2.319529e-06, 2.319529e-06, 2.319529e-06]
     }
)

gromos_atp_c6 = pd.DataFrame(
    {'name': ['O', 'CH1', 'CH2', 'CH3'],
     'c6': [0.0022619536, 0.00606841, 0.0074684164, 0.0096138025]
    }
)

# Harp 2
#gromos_atp = pd.DataFrame(
#    {'name': ['O', 'OA', 'N', 'C', 'CH1', 
#            'CH2', 'CH3', 'CH2r', 'NT', 'S',
#            'NR', 'OM', 'NE', 'NL', 'NZ'],
#     'mass': [16, 17, 15, 12, 13, 14, 15, 14, 17, 32, 14, 16, 15, 17, 16],
#     'at.num': [8, 8, 7, 6, 6, 6, 6, 6, 7, 16, 7, 8, 7, 7, 7],
#     'c12': [1e-06, 3.011058e-06, # H OA
#     4.639058e-06, # H N
#      4.937284e-06, 9.70225e-05, # CH1
#            3.3965584e-05, 2.6646244e-05, 2.8058209e-05, 10.125e-06, # H NT
#            1.3075456e-05,
#            3.389281e-06, 7.4149321e-07, 2.319529e-06, 2.319529e-06, 2.319529e-06]
#     }
#)

# Harp 2.5
#gromos_atp = pd.DataFrame(
#    {'name': ['O', 'OA', 'N', 'C', 'CH1', 
#            'CH2', 'CH3', 'CH2r', 'NT', 'S',
#            'NR', 'OM', 'NE', 'NL', 'NZ'],
#     'mass': [16, 17, 15, 12, 13, 14, 15, 14, 17, 32, 14, 16, 15, 17, 16],
#     'at.num': [8, 8, 7, 6, 6, 6, 6, 6, 7, 16, 7, 8, 7, 7, 7],
#     'c12': [1e-06, 3.7638225e-06, # H OA
#     5.7988225e-06, # H N
#      4.937284e-06, 9.70225e-05, # CH1
#            3.3965584e-05, 2.6646244e-05, 2.8058209e-05, 12.65625e-06, # H NT
#            1.3075456e-05,
#            3.389281e-06, 7.4149321e-07, 2.319529e-06, 2.319529e-06, 2.319529e-06]
#     }
#)

# Harp max (20)
#gromos_atp = pd.DataFrame(
#    {'name': ['O', 'OA', 'N', 'C', 'CH1', 'CH2', 'CH3', 'CH2r', 'NT', 'S', 'NR', 'OM', 'NE', 'NL', 'NZ'],
#     'mass': [16, 17, 15, 12, 13, 14, 15, 14, 17, 32, 14, 16, 15, 17, 16],
#     'at.num': [8, 8, 7, 6, 6, 6, 6, 6, 7, 16, 7, 8, 7, 7, 7],
#     'c12': [1e-06, 3.011e-05, 4.639e-05, 4.937284e-06, 9.70225e-05,
#            3.3965584e-05, 2.6646244e-05, 2.8058209e-05, 1.2e-05, 1.3075456e-05,
#            3.389281e-06, 7.4149321e-07, 2.319529e-06, 2.319529e-06, 2.319529e-06]
#     }
#)


gromos_atp.to_dict()
gromos_atp.set_index('name', inplace=True)

gromos_atp_c6.to_dict()
gromos_atp_c6.set_index('name', inplace=True)

# BONDS
# This list will be used to build pairs and exclusions lists to attach in the topology
atom_types, atom_resids = protein.list_bonds(by_resid=True)
ai_type, aj_type, ai_resid, aj_resid = [], [], [], []

for atyp in atom_types:
    atyp_split = atyp.split(' ')
    ai_type.append(atyp_split[0])
    aj_type.append(atyp_split[1])

for ares in atom_resids:
    ares_split = ares.split(' ')
    ai_resid.append(ares_split[0])
    aj_resid.append(ares_split[1])



# TODO put the exclusion list here instead of the function
# TODO you also may consider to join the exclusions and pairs function within the atomtypes one

topology_bonds = pd.DataFrame(np.column_stack([ai_type, ai_resid, aj_type, aj_resid]), columns=['ai_type', 'ai_resid','aj_type', 'aj_resid'])
topology_bonds['ai'] = topology_bonds['ai_type'] + '_' + topology_bonds['ai_resid'].astype(str)
topology_bonds['aj'] = topology_bonds['aj_type'] + '_' + topology_bonds['aj_resid'].astype(str)
topology_bonds.drop(['ai_type', 'ai_resid','aj_type', 'aj_resid'], axis=1, inplace=True)

# TODO make it more general (without the residue number)
# native reweight for TTR and ABeta. This dictionary will rename the amber topology to gromos topology

#gro_to_amb_dict = {'OC1_11' : 'O1_11', 'OC2_11':'O2_11'}
gro_to_amb_dict = {'OT1_42' : 'O1_42', 'OT2_42':'O2_42'}
