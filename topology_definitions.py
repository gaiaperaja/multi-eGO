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
topology_atoms = pd.DataFrame(np.column_stack([atom_topology_num, atom_topology_type, atom_topology_resid, atom_topology_resname, atom_topology_name, atom_topology_mass]), columns=['nr', 'type','resnr', 'residue', 'atom', 'mass'])

# Changing the mass of the atoms section by adding the H
topology_atoms['mass'].astype(float)
mask = ((topology_atoms['type'] == 'N') | (topology_atoms['type'] == 'OA')) | ((topology_atoms['residue'] == 'TYR') & ((topology_atoms['atom'] == 'CD1') | (topology_atoms['atom'] == 'CD2') | (topology_atoms['atom'] == 'CE1') | (topology_atoms['atom'] == 'CE2')))
topology_atoms['mass'][mask] = topology_atoms['mass'][mask].astype(float).add(1)
# Same thing here with the N terminal which is charged
mask = (topology_atoms['resnr'] == '1') & (topology_atoms['atom'] == 'N')
topology_atoms['mass'][mask] = topology_atoms['mass'][mask].astype(float).add(2)
# Structure based atomtype definition
topology_atoms['sb_type'] = topology_atoms['atom'] + '_' + topology_atoms['resnr']


# ACID pH
# Selection of the aminoacids and the charged atoms
acid_ASP = topology_atoms[(topology_atoms['residue'] == "ASP") & ((topology_atoms['atom'] == "OD1") | (topology_atoms['atom'] == "OD2") | (topology_atoms['atom'] == "CG"))]
acid_GLU = topology_atoms[(topology_atoms['residue'] == "GLU") & ((topology_atoms['atom'] == "OE1") | (topology_atoms['atom'] == "OE2") | (topology_atoms['atom'] == "CD"))]
acid_HIS = topology_atoms[(topology_atoms['residue'] == "HIS") & ((topology_atoms['atom'] == "ND1") | (topology_atoms['atom'] == "CE1") | (topology_atoms['atom'] == "NE2") | (topology_atoms['atom'] == "CD2") | (topology_atoms['atom'] == "CG"))]
frames = [acid_ASP, acid_GLU, acid_HIS]
acid_atp = pd.concat(frames, ignore_index = True)
acid_atp = acid_atp['sb_type'].tolist()

# THE C12 RATIO CHANGED a little bit.atomtypes
gromos_atp = pd.DataFrame(
    {'name': ['O', 'OA', 'N', 'C', 'CH1', 'CH2', 'CH3', 'CH2r', 'NT', 'S', 'NR', 'OM', 'NE', 'NL', 'NZ'],
     'mass': [16, 17, 15, 12, 13, 14, 15, 14, 17, 32, 14, 16, 15, 17, 16],
     'at.num': [8, 8, 7, 6, 6, 6, 6, 6, 7, 16, 7, 8, 7, 7, 7],
     'c12': [1e-06, 3.011e-05, 4.639e-05, 4.937284e-06, 9.70225e-05,
            3.3965584e-05, 2.6646244e-05, 2.8058209e-05, 1.2e-05, 1.3075456e-05,
            3.389281e-06, 7.4149321e-07, 2.319529e-06, 2.319529e-06, 2.319529e-06]
     }
)

gromos_atp.to_dict()
gromos_atp.set_index('name', inplace=True)

# Unfortunately those are strings which must be separated
# BONDS
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

topology_bonds = pd.DataFrame(np.column_stack([ai_type, ai_resid, aj_type, aj_resid]), columns=['ai_type', 'ai_resid','aj_type', 'aj_resid'])
topology_bonds['ai'] = topology_bonds['ai_type'] + '_' + topology_bonds['ai_resid'].astype(str)
topology_bonds['aj'] = topology_bonds['aj_type'] + '_' + topology_bonds['aj_resid'].astype(str)
topology_bonds.drop(['ai_type', 'ai_resid','aj_type', 'aj_resid'], axis=1, inplace=True)

# ANGLES
atom_types, atom_resids = protein.list_angles(by_resid=True)
ai_type, aj_type, ak_type, ai_resid, aj_resid, ak_resid = [], [], [], [], [], []

for atyp in atom_types:
    atyp_split = atyp.split(' ')
    ai_type.append(atyp_split[0])
    aj_type.append(atyp_split[1])
    ak_type.append(atyp_split[2])

for ares in atom_resids:
    ares_split = ares.split(' ')
    ai_resid.append(ares_split[0])
    aj_resid.append(ares_split[1])
    ak_resid.append(ares_split[2])

topology_angles = pd.DataFrame(np.column_stack([ai_type, ai_resid, aj_type, aj_resid, ak_type, ak_resid]), columns=['ai_type', 'ai_resid', 'aj_type', 'aj_resid', 'ak_type', 'ak_resid'])
topology_angles['ai'] = topology_angles['ai_type'] + '_' + topology_angles['ai_resid'].astype(str)
topology_angles['aj'] = topology_angles['aj_type'] + '_' + topology_angles['aj_resid'].astype(str)
topology_angles['ak'] = topology_angles['ak_type'] + '_' + topology_angles['ak_resid'].astype(str)
topology_angles.drop(['ai_type', 'ai_resid','aj_type', 'aj_resid', 'ak_type', 'ak_resid'], axis=1, inplace=True)

# DIHEDRALS
atom_types, atom_resids = protein.list_dihedrals(by_resid=True)
ai_type, aj_type, ak_type, al_type, ai_resid, aj_resid, ak_resid, al_resid = [], [], [], [], [], [], [], []

for atyp in atom_types:
    atyp_split = atyp.split(' ')
    ai_type.append(atyp_split[0])
    aj_type.append(atyp_split[1])
    ak_type.append(atyp_split[2])
    al_type.append(atyp_split[3])

for ares in atom_resids:
    ares_split = ares.split(' ')
    ai_resid.append(ares_split[0])
    aj_resid.append(ares_split[1])
    ak_resid.append(ares_split[2])
    al_resid.append(ares_split[3])

topology_dihedrals = pd.DataFrame(np.column_stack([ai_type, ai_resid, aj_type, aj_resid, ak_type, ak_resid, al_type, al_resid]), columns=['ai_type', 'ai_resid', 'aj_type', 'aj_resid', 'ak_type', 'ak_resid', 'al_type', 'al_resid'])
topology_dihedrals['ai'] = topology_dihedrals['ai_type'] + '_' + topology_dihedrals['ai_resid'].astype(str)
topology_dihedrals['aj'] = topology_dihedrals['aj_type'] + '_' + topology_dihedrals['aj_resid'].astype(str)
topology_dihedrals['ak'] = topology_dihedrals['ak_type'] + '_' + topology_dihedrals['ak_resid'].astype(str)
topology_dihedrals['al'] = topology_dihedrals['al_type'] + '_' + topology_dihedrals['al_resid'].astype(str)
topology_dihedrals.drop(['ai_type', 'ai_resid','aj_type', 'aj_resid', 'ak_type', 'ak_resid', 'al_type', 'al_resid'], axis=1, inplace=True)

# IMPROPERS
atom_types, atom_resids = protein.list_impropers(by_resid=True)
ai_type, aj_type, ak_type, al_type, ai_resid, aj_resid, ak_resid, al_resid = [], [], [], [], [], [], [], []

for atyp in atom_types:
    atyp_split = atyp.split(' ')
    ai_type.append(atyp_split[0])
    aj_type.append(atyp_split[1])
    ak_type.append(atyp_split[2])
    al_type.append(atyp_split[3])

for ares in atom_resids:
    ares_split = ares.split(' ')
    ai_resid.append(ares_split[0])
    aj_resid.append(ares_split[1])
    ak_resid.append(ares_split[2])
    al_resid.append(ares_split[3])

topology_impropers = pd.DataFrame(np.column_stack([ai_type, ai_resid, aj_type, aj_resid, ak_type, ak_resid, al_type, al_resid]), columns=['ai_type', 'ai_resid', 'aj_type', 'aj_resid', 'ak_type', 'ak_resid', 'al_type', 'al_resid'])
topology_impropers['ai'] = topology_impropers['ai_type'] + '_' + topology_impropers['ai_resid'].astype(str)
topology_impropers['aj'] = topology_impropers['aj_type'] + '_' + topology_impropers['aj_resid'].astype(str)
topology_impropers['ak'] = topology_impropers['ak_type'] + '_' + topology_impropers['ak_resid'].astype(str)
topology_impropers['al'] = topology_impropers['al_type'] + '_' + topology_impropers['al_resid'].astype(str)
topology_impropers.drop(['ai_type', 'ai_resid','aj_type', 'aj_resid', 'ak_type', 'ak_resid', 'al_type', 'al_resid'], axis=1, inplace=True)


# Exclusion list creation

# It takes the column names of each dataframe (ai, aj, ak, al) and make combinations pairwise.
# From the dataframes takes the ai aj columns and marges making the atom combination and assigning to a list

for c in list(itertools.combinations((topology_bonds.columns.values.tolist()), 2)):
    exclusion_list_gromologist.append((topology_bonds[c[0]] + '_' + topology_bonds[c[1]]).to_list())
    exclusion_list_gromologist.append((topology_bonds[c[1]] + '_' + topology_bonds[c[0]]).to_list())

for c in list(itertools.combinations((topology_angles.columns.values.tolist()), 2)):
    exclusion_list_gromologist.append((topology_angles[c[0]] + '_' + topology_angles[c[1]]).to_list())
    exclusion_list_gromologist.append((topology_angles[c[1]] + '_' + topology_angles[c[0]]).to_list())

for c in list(itertools.combinations((topology_dihedrals.columns.values.tolist()), 2)):
    exclusion_list_gromologist.append((topology_dihedrals[c[0]] + '_' + topology_dihedrals[c[1]]).to_list())
    exclusion_list_gromologist.append((topology_dihedrals[c[1]] + '_' + topology_dihedrals[c[0]]).to_list())

for c in list(itertools.combinations((topology_impropers.columns.values.tolist()), 2)):
    exclusion_list_gromologist.append((topology_impropers[c[0]] + '_' + topology_impropers[c[1]]).to_list())
    exclusion_list_gromologist.append((topology_impropers[c[1]] + '_' + topology_impropers[c[0]]).to_list())

# The resulting list is a list of lists and this makes a unique flat one
exclusion_list_gromologist = [item for sublist in exclusion_list_gromologist for item in sublist]
#print(len(flat_exclusion_list_gromologist)) # 2238

exclusion_list_gromologist = list(set(exclusion_list_gromologist))
#print(len(exclusion_list_gromologist)) # 514