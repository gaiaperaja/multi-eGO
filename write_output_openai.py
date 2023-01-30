from time import localtime, strftime
import pandas as pd


pd.set_option('display.colheader_justify', 'right')

def get_name(parameters):
    name = f'{parameters.protein}_{parameters.egos}_e{parameters.epsilon}_{parameters.inter_epsilon}'
    return name


def dataframe_to_write(df):
    df.rename(columns={df.columns[0]: f"; {df.columns[0]}"}, inplace = True)
    return df.to_string(index=False)


def make_header(parameters):
    now = strftime("%d-%m-%Y %H:%M", localtime())
    
    header = f'''
; Multi-eGO force field provided by Emanuele Scalone and Carlo Camilloni at Camilloni Lab
; Created on the {now}
; With the following parameters:
'''
    for parameter, value in parameters.items():
        if type(value) is list:
            header += ';\t- {:<15} = {:<20}\n'.format(parameter, ", ".join(value))
        else:
            header += ';\t- {:<15} = {:<20}\n'.format(parameter, value)
    return header


def write_topology(topology_dataframe, bonded_interactions_dict, lj_14, parameters, output_folder):
    molecule_footer = []
    header = make_header(vars(parameters))
    file = open(f'{output_folder}/topol_GRETA.top', 'w')
    header += '''
; Include forcefield parameters
#include "multi-ego-basic.ff/forcefield.itp"
'''

    file.write(header)
    for molecule, bonded_interactions in bonded_interactions_dict.items():
        molecule_footer.append(molecule)
        
        molecule_header = f'''
[ moleculetype ]
; Name\tnrexcl
{molecule}\t\t\t3

'''

        file.write(molecule_header)
        file.write('[ atoms ]\n')
        atom_selection_dataframe = topology_dataframe.loc[topology_dataframe['molecule_name'] == molecule][['number', 'sb_type', 'resnum', 'resname', 'name', 'cgnr']].copy()
        file.write(f'{dataframe_to_write(atom_selection_dataframe)}\n\n')
        # Here are written bonds, angles, dihedrals and impropers
        for type, interactions in bonded_interactions.items():
            if interactions.empty:
                continue
            else:
                file.write(f'[ {type} ]\n')
                file.write(dataframe_to_write(interactions))
                file.write('\n\n')
        # Here are written pairs and exclusions



    footer = f'''
; Include Position restraint file
#ifdef POSRES
#include "posre.itp"
#endif

[ system ]
{parameters.protein}

[ molecules ]
; Compound #mols
'''

    file.write(footer)
    for molecule in molecule_footer:
        file.write(f'{molecule}\t\t\t1\n')
    
    file.close()


def write_nonbonded(topology_dataframe, lj_potential, parameters, output_folder):
    #pd.set_option('display.colheader_justify', 'right')
    header = make_header(vars(parameters))
    
    file = open(f'{output_folder}/ffnonbonded.itp', 'w')
    file.write(header)
    file.write('[ atomtypes ]\n')
    file.write(dataframe_to_write(topology_dataframe[['sb_type', 'atomic_number', 'mass', 'charge', 'ptype', 'c6', 'c12']].copy()))
    file.write("\n\n[ nonbond_params ]\n")
    file.write(lj_potential.to_string())

