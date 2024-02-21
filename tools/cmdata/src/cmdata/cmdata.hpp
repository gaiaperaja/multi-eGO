#ifndef _CMDATA_CMDATA_HPP
#define _CMDATA_CMDATA_HPP

#include "gromacs/analysisdata/analysisdata.h"
#include "gromacs/selection/selection.h"
#include "gromacs/selection/selectionoption.h"
#include "gromacs/trajectory/trajectoryframe.h"
#include "gromacs/trajectoryanalysis/analysissettings.h"
#include "gromacs/trajectoryanalysis/topologyinformation.h"
#include "gromacs/math/vec.h"
#include "gromacs/pbcutil/pbc.h"
#include "gromacs/fileio/tpxio.h"
#include "gromacs/fileio/trxio.h"
#include "gromacs/fileio/confio.h"

#include "gromacs/utility/futil.h"
#include "gromacs/utility/gmxassert.h"
#include "gromacs/utility/smalloc.h"
#include "gromacs/fileio/oenv.h"

#include "io.hpp"
#include "indexing.hpp"
#include "parallel.hpp"
#include "density.hpp"
#include "mindist.hpp"
#include "xtc_frame.hpp"
#include "function_types.hpp"

// standard library imports
#include <iostream>
#include <omp.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <cmath>
#include <memory>
#include <string>
#include <algorithm>
#include <functional>
#include <numeric>
#include <sstream>
#include <fstream>

#include <xdrfile.h>
#include <xdrfile_xtc.h>

namespace cmdata
{

class CMData
{
private:
  gmx::Selection refsel_;
  double cutoff_;
  double mol_cutoff_;
  int n_x_;
  unsigned long int nframe_;
  int nskip_;
  rvec *xcm_ = nullptr;
  gmx_mtop_t *mtop_ = NULL;
  std::vector<int> mol_id_;
  std::vector<double> inv_num_mol_;
  std::vector<double> inv_num_mol_unique_;
  std::string sym_file_path_;
  std::vector<std::vector<std::vector<int>>> equivalence_list_;
  bool list_sym_;
  std::string list_sym_path_;
  std::vector<int> num_mol_unique_;
  std::vector<double> weights_;
  std::string weights_path_;
  double weights_sum_;
  bool no_pbc_;
  t_pbc *pbc_;
  rvec *x_;
  // frame fields
  cmdata::xtc::Frame *frame_;
  XDRFILE *trj_;
  float dt_;

  std::vector<int> natmol2_;
  int nindex_;
  gmx::RangePartitioning mols_;
  std::vector<std::vector<int>> cross_index_;
  std::vector<t_atoms> molecules_;
  std::vector<double> density_bins_;
  std::size_t n_bins_;
  int num_threads_;

  double mcut2_;
  double cut_sig_2_;
  double dx_;

  using cmdata_matrix = std::vector<std::vector<std::vector<std::vector<double>>>>;
  cmdata_matrix interm_same_mat_density_;
  cmdata_matrix interm_cross_mat_density_;
  cmdata_matrix intram_mat_density_;
  cmdata_matrix interm_same_maxcdf_mol_;
  cmdata_matrix interm_cross_maxcdf_mol_;

  // temporary containers for maxcdf operations
  std::vector<std::vector<double>> frame_same_mat_;
  std::vector<std::vector<double>> frame_cross_mat_;
  std::vector<std::vector<std::mutex>> frame_same_mutex_;
  std::vector<std::vector<std::mutex>> frame_cross_mutex_;
  std::vector<std::thread> threads_;
  std::vector<std::thread> mol_threads_;
  cmdata::parallel::Semaphore semaphore_;

  // mode selection, booleans and functions
  std::string mode_;
  bool intra_ = false, same_ = false, cross_ = false;

  // function types
  using ftype_intra_ = cmdata::ftypes::function_traits<decltype(&cmdata::density::intra_mol_routine)>;
  using ftype_same_ = cmdata::ftypes::function_traits<decltype(&cmdata::density::inter_mol_same_routine)>;
  using ftype_cross_ = cmdata::ftypes::function_traits<decltype(&cmdata::density::inter_mol_cross_routine)>;

  std::function<ftype_intra_::signature> f_intra_mol_;
  std::function<ftype_same_::signature> f_inter_mol_same_;
  std::function<ftype_cross_::signature> f_inter_mol_cross_;

  static void mindist_kernel(
    double weight,                            // common parameters
    const std::vector<int> &natmol2,
    const std::vector<double> &density_bins,
    const std::vector<int> &num_mol_unique,
    std::size_t start_mti_same,               // same parameters
    std::size_t start_im_same,
    std::size_t start_i_same,
    std::size_t start_j_same,
    long int n_loop_operations_same,
    const std::vector<std::vector<double>> &frame_same_mat,
    std::vector<std::vector<std::mutex>> &frame_same_mutex, 
    cmdata_matrix &interm_same_maxcdf_mol,
    std::size_t start_mti_cross,              // cross parameters
    std::size_t start_mtj_cross,
    std::size_t start_im_cross,
    std::size_t start_jm_cross,
    std::size_t start_i_cross,
    std::size_t start_j_cross,
    int n_loop_operations_cross,
    const std::vector<std::vector<int>> &cross_index, 
    const std::vector<std::vector<double>> &frame_cross_mat,
    std::vector<std::vector<std::mutex>> &frame_cross_mutex,
    cmdata_matrix &interm_cross_maxcdf_mol
  )
  {
    if ( n_loop_operations_same != 0 )
    {
      cmdata::mindist::mindist_same(
        start_mti_same, start_im_same, start_i_same, start_j_same, n_loop_operations_same, density_bins,
        num_mol_unique, natmol2, frame_same_mat, frame_same_mutex, interm_same_maxcdf_mol, weight
      );
    }

    if ( n_loop_operations_cross != 0 )
    {
      cmdata::mindist::mindist_cross(
        start_mti_cross, start_mtj_cross, start_im_cross, start_jm_cross, start_i_cross, start_j_cross, n_loop_operations_cross, 
        natmol2, cross_index, density_bins, num_mol_unique, frame_cross_mat, frame_cross_mutex, interm_cross_maxcdf_mol, weight
      );
    }
  }

  static void molecule_routine(
    const int i, const int nindex_, t_pbc *pbc, rvec *x, const std::vector<double> &inv_num_mol_, const double cut_sig_2_, 
    const std::vector<int> &natmol2_, const std::vector<int> &num_mol_unique_, const std::vector<int> &mol_id_, 
    const std::vector<std::vector<int>> &cross_index_, const std::vector<double> &density_bins_, const double mcut2_, 
    rvec *xcm_, const gmx::RangePartitioning &mols_, gmx_mtop_t *mtop_, const std::vector<std::vector<std::vector<int>>> &equivalence_list_,
    std::vector<std::vector<double>> &frame_same_mat_, std::vector<std::vector<std::mutex>> &frame_same_mutex_,
    cmdata_matrix &intram_mat_density_, cmdata_matrix &interm_same_mat_density_, std::vector<std::vector<double>> &frame_cross_mat_,
    std::vector<std::vector<std::mutex>> &frame_cross_mutex_, cmdata_matrix &interm_cross_mat_density_, cmdata::parallel::Semaphore &semaphore_,
    const std::function<ftype_intra_::signature> &f_intra_mol_, const std::function<ftype_same_::signature> &f_inter_mol_same_,
    const std::function<ftype_cross_::signature> &f_inter_mol_cross_, double weight
  )
  {
    const char * atomname;
    int tmp_i = 0;
    std::size_t mol_i = i, mol_j = 0;
    while ( static_cast<int>(mol_i) - num_mol_unique_[tmp_i] >= 0  )
    {
      mol_i -= num_mol_unique_[tmp_i];
      tmp_i++;
      if (tmp_i == num_mol_unique_.size()) break;
    }
    if (mol_i == num_mol_unique_[mol_id_[i]]) mol_i = 0;
    int molb = 0;
    semaphore_.acquire();
    /* Loop over molecules  */
    for (int j = 0; j < nindex_; j++)
    {
      if (j!=0)
        if (mol_j == num_mol_unique_[mol_id_[j-1]]) mol_j = 0;

      /* intermolecular interactions are evaluated only among neighbour molecules */
      if (i!=j)
      {
        rvec dx;
        if (pbc != nullptr) pbc_dx(pbc, xcm_[i], xcm_[j], dx);
        else rvec_sub(xcm_[i], xcm_[j], dx);
        double dx2 = iprod(dx, dx);
        if (dx2 > mcut2_) continue;
      }
      /* for molecules of different specie we fill half a matrix */
      if (mol_id_[i] != mol_id_[j] && j < i) continue;
      std::size_t a_i = 0;
      GMX_RELEASE_ASSERT(mols_.numBlocks() > 0, "Cannot access index[] from empty mols");

      /* cycle over the atoms of a molecule i */
      for (std::size_t ii = mols_.block(i).begin(); ii < mols_.block(i).end(); ii++)
      {
        std::size_t a_j = 0;
        mtopGetAtomAndResidueName(*mtop_, ii, &molb, &atomname, nullptr, nullptr, nullptr);
        if (atomname[0] == 'H')
        {
          a_i++;
          continue;
        }
        /* cycle over the atoms of a molecule j */
        for (std::size_t jj = mols_.block(j).begin(); jj < mols_.block(j).end(); jj++)
        {
          mtopGetAtomAndResidueName(*mtop_, jj, &molb, &atomname, nullptr, nullptr, nullptr);
          if (atomname[0] == 'H')
          {
            a_j++;
            continue;
          }
          // check for chemical equivalence
          double nsym = static_cast<double>(equivalence_list_[mol_id_[i]][a_i].size()*equivalence_list_[mol_id_[j]][a_j].size());
          if(i==j&&a_i!=a_j)
          {
            // this is to account for the correct normalisation in the case in which
            // an intramolecular interaction is between two atoms that are also equivalent
            for (std::size_t eq_i = 0; eq_i < equivalence_list_[mol_id_[i]][a_i].size(); eq_i++)
            {
              for (std::size_t eq_j = 0; eq_j < equivalence_list_[mol_id_[j]][a_j].size(); eq_j++)
              {
                // get molecule-wise atom index considering equivalence
                std::size_t eqa_i  = equivalence_list_[mol_id_[i]][a_i][eq_i];             // molecule-wise equivalence index i
                std::size_t geqa_i = ii + (eqa_i - equivalence_list_[mol_id_[i]][a_i][0]); // global equivalence index i
                std::size_t eqa_j  = equivalence_list_[mol_id_[j]][a_j][eq_j];             // molecule-wise equivalence index j
                std::size_t geqa_j = jj + (eqa_j - equivalence_list_[mol_id_[j]][a_j][0]); // global equivalence index j
                if(geqa_i==geqa_j) nsym=nsym-1.0;
              }
            }
          }
          for (std::size_t eq_i = 0; eq_i < equivalence_list_[mol_id_[i]][a_i].size(); eq_i++)
          {
            for (std::size_t eq_j = 0; eq_j < equivalence_list_[mol_id_[j]][a_j].size(); eq_j++)
            {
              // get molecule-wise atom index considering equivalence
              std::size_t eqa_i  = equivalence_list_[mol_id_[i]][a_i][eq_i];             // molecule-wise equivalence index i
              std::size_t geqa_i = ii + (eqa_i - equivalence_list_[mol_id_[i]][a_i][0]); // global equivalence index i
              std::size_t eqa_j  = equivalence_list_[mol_id_[j]][a_j][eq_j];             // molecule-wise equivalence index j
              std::size_t geqa_j = jj + (eqa_j - equivalence_list_[mol_id_[j]][a_j][0]); // global equivalence index j
              std::size_t delta  = eqa_i - eqa_j;
              if(i==j&&a_i==a_j) {
                // this is the special case of intra-self that should not be symmetrized
                // the distance of an atom with itself cannot be greater than 0.
                geqa_i=ii;
                geqa_j=jj;
              }
              if(i==j&&a_i!=a_j&&geqa_i==geqa_j) continue;
              rvec sym_dx;
              if (pbc != nullptr) pbc_dx(pbc, x[geqa_i], x[geqa_j], sym_dx);
              else rvec_sub(x[geqa_i], x[geqa_j], sym_dx);
              double dx2 = iprod(sym_dx, sym_dx);
              if(i==j) 
              {
                if (dx2 < cut_sig_2_)
                { // intra molecule species
                  f_intra_mol_(i, a_i, a_j, dx2, weight, nsym, mol_id_, natmol2_, density_bins_, inv_num_mol_, frame_same_mutex_, intram_mat_density_);
                }
              }
              else
              {
                if(mol_id_[i]==mol_id_[j])
                { // inter same molecule specie
                  if (dx2 < cut_sig_2_)
                  {
                    f_inter_mol_same_(
                      i, mol_i, a_i, a_j, dx2, weight, mol_id_, natmol2_, density_bins_, frame_same_mutex_, frame_same_mat_, interm_same_mat_density_
                    );
                  }
                  if(delta!=0.) {
                    // this is to account for inversion atom/molecule
                    if (pbc != nullptr) pbc_dx(pbc, x[geqa_i-delta], x[geqa_j+delta], sym_dx);
                    else rvec_sub(x[geqa_i-delta], x[geqa_j+delta], sym_dx);
                    dx2 = iprod(sym_dx, sym_dx);
                    if (dx2 < cut_sig_2_)
                    {
                      f_inter_mol_same_(
                        i, mol_i, a_i, a_j, dx2, weight, mol_id_, natmol2_, density_bins_, frame_same_mutex_, frame_same_mat_, interm_same_mat_density_
                      );
                    }
                  }
                } 
                else
                { // inter cross molecule species
                  if (dx2 < cut_sig_2_)
                  {
                    f_inter_mol_cross_(
                      i, j, mol_i, mol_j, a_i, a_j, dx2, weight, mol_id_, natmol2_, cross_index_, density_bins_, frame_cross_mutex_, frame_cross_mat_, interm_cross_mat_density_
                    );
                  }
                }
              }
            }
          }
          ++a_j;
        }
        ++a_i;
      }
      ++mol_j;
    }
    semaphore_.release();
  }

public:
  CMData(
    const std::string &top_path, const std::string &traj_path,
    double cutoff, double mol_cutoff, int nskip, int num_threads,
    int dt, const std::string &mode, const std::string &weights_path, 
    const std::string &sym_file_path, bool list_sym, bool no_pbc
  ) : cutoff_(cutoff), mol_cutoff_(mol_cutoff), nskip_(nskip), num_threads_(num_threads),
      mode_(mode), weights_path_(weights_path), sym_file_path_(sym_file_path), list_sym_(list_sym), no_pbc_(no_pbc), dt_(dt)
  {
    bool bTop_;
    PbcType pbcType_;
    matrix boxtop_;
    mtop_ = (gmx_mtop_t*)malloc(sizeof(gmx_mtop_t));
    // std::unique_ptr<gmx_mtop_t> mt = std::make_unique<gmx_mtop_t>();
    // readConfAndTopology(top_path.c_str(), &bTop_, mtop_, &pbcType_, nullptr, nullptr, boxtop_);
    // readConfAndTopology(top_path.c_str(), &bTop_, mt.get(), &pbcType_, nullptr, nullptr, boxtop_);
    TpxFileHeader header = readTpxHeader(top_path.c_str(), true);
    int natoms;
    pbcType_ = read_tpx(top_path.c_str(), nullptr, boxtop_, &natoms, nullptr, nullptr, mtop_);

    if (no_pbc_)
    {
      pbc_ = nullptr;
    }
    else
    {
      pbc_ = (t_pbc*)malloc(sizeof(t_pbc));
      set_pbc(pbc_, pbcType_, boxtop_);
    }

    int natom;
    long unsigned int nframe;
    int64_t *offsets;

    frame_ = (cmdata::xtc::Frame*)malloc(sizeof(cmdata::xtc::Frame));
    std::cout << "Reading trajectory file " << traj_path << std::endl;
    read_xtc_header(traj_path.c_str(), &natom, &nframe, &offsets);
    *frame_ = cmdata::xtc::Frame(natom);
    frame_->nframe = nframe;
    frame_->offsets = offsets;

    trj_ = xdrfile_open(traj_path.c_str(), "r");
    initAnalysis();
  }

  ~CMData()
  {
    free(frame_->x);
    free(frame_->offsets);
    free(frame_);
    // xdrfile_close(trj_);
    // free(pbc_);
    // free(mtop_);
  }

  void initAnalysis()
  {
    n_x_ = 0;
    nframe_ = 0;

    // get the number of atoms per molecule
    // equivalent to mols_ = gmx:gmx_mtop_molecules(*top.mtop());
    for (const gmx_molblock_t &molb : mtop_->molblock)
    {
      int natm_per_mol = mtop_->moltype[molb.type].atoms.nr;
      for (int i = 0; i < molb.nmol; i++) mols_.appendBlock(natm_per_mol);
    }
    // number of molecules
    nindex_ = mols_.numBlocks();

    if ( num_threads_ > std::thread::hardware_concurrency() )
    {
      num_threads_ = std::thread::hardware_concurrency();
      std::cout << "Maximum thread number surpassed. Scaling num_threads down to " << num_threads_ << std::endl;
    }
    threads_.resize(num_threads_);
    mol_threads_.resize(nindex_);
    semaphore_.set_counter(std::min(num_threads_, nindex_));
    std::cout << "Using " << num_threads_ << " threads" << std::endl;

    // set up mode selection
    f_intra_mol_ = cmdata::ftypes::do_nothing<ftype_intra_>();
    f_inter_mol_same_ = cmdata::ftypes::do_nothing<ftype_same_>();
    f_inter_mol_cross_ = cmdata::ftypes::do_nothing<ftype_cross_>();

    printf("Evaluating mode selection:\n");
    std::string tmp_mode;
    std::stringstream modestream{ mode_ };
    while (std::getline(modestream, tmp_mode, '+'))
    {
      printf(" - found %s", tmp_mode.c_str());
      if ( tmp_mode == std::string("intra") )
      {
        intra_ = true;
        f_intra_mol_ = cmdata::density::intra_mol_routine;
        printf(" :: activating intramat calculations\n");
      }
      else if ( tmp_mode == std::string("same") )
      {
        same_ = true;
        f_inter_mol_same_ = cmdata::density::inter_mol_same_routine;
        printf(" :: activating intermat same calculations\n");
      }
      else if ( tmp_mode == std::string("cross") )
      {
        cross_ = true;
        f_inter_mol_cross_ = cmdata::density::inter_mol_cross_routine;
        printf(" :: activating intermat cross calculations\n");
      }
      else printf(" :: ignoring keyword\n");
    }

    std::vector<int> num_mol;
    num_mol.push_back(1);
    int num_unique_molecules = 0;
    // number of atoms per molecule, assuming them identical when consecutive molecules have the same number of atoms
    natmol2_.push_back(mols_.block(0).end());
    for (int i = 1; i < nindex_; i++)
    {
      natmol2_.push_back(mols_.block(i).end() - mols_.block(i - 1).end());
      if (natmol2_[i] == natmol2_[i - 1]) num_mol[num_unique_molecules]++;
      else
      {
        num_mol.push_back(1);
        num_unique_molecules++;
      }
    }
    std::vector<int>::iterator it = std::unique(natmol2_.begin(), natmol2_.end());
    natmol2_.resize(std::distance(natmol2_.begin(), it));

    std::vector<int> start_index;
    mol_id_.push_back(0);
    start_index.push_back(0);
    num_unique_molecules = 0;
    inv_num_mol_.push_back(1. / (static_cast<double>(num_mol[num_unique_molecules])));
    num_mol_unique_.push_back(num_mol[num_unique_molecules]);
    inv_num_mol_unique_.push_back(1. / (static_cast<double>(num_mol[num_unique_molecules])));
    for (int i = 1; i < nindex_; i++)
    {
      if (mols_.block(i).end() - mols_.block(i - 1).end() == natmol2_[num_unique_molecules])
      {
        start_index.push_back(start_index[i - 1]);
      }
      else
      {
        start_index.push_back(natmol2_[num_unique_molecules]);
        num_unique_molecules++;
        inv_num_mol_unique_.push_back(1. / (static_cast<double>(num_mol[num_unique_molecules])));
        num_mol_unique_.push_back(num_mol[num_unique_molecules]);
      }
      mol_id_.push_back(num_unique_molecules);
      inv_num_mol_.push_back(1. / static_cast<double>(num_mol[num_unique_molecules]));
    }

    printf("number of different molecules %lu\n", natmol2_.size());
    for(std::size_t i=0; i<natmol2_.size();i++) printf("mol %lu num %u size %u\n", i, num_mol[i], natmol2_[i]);

    if (same_)
    {
      interm_same_mat_density_.resize(natmol2_.size());
      interm_same_maxcdf_mol_.resize(natmol2_.size());
    }
    if (cross_)
    {
      interm_cross_mat_density_.resize((natmol2_.size() * (natmol2_.size() - 1)) / 2);
      interm_cross_maxcdf_mol_.resize((natmol2_.size() * (natmol2_.size() - 1)) / 2);
    }
    if (intra_) intram_mat_density_.resize(natmol2_.size());

    density_bins_.resize(cmdata::indexing::n_bins(cutoff_));
    for (std::size_t i = 0; i < density_bins_.size(); i++)
      density_bins_[i] = cutoff_ / static_cast<double>(density_bins_.size()) * static_cast<double>(i) + cutoff_ / static_cast<double>(density_bins_.size() * 2);

    int cross_count = 0;
    if (cross_) cross_index_.resize(natmol2_.size(), std::vector<int>(natmol2_.size(), 0));
    for ( std::size_t i = 0; i < natmol2_.size(); i++ )
    {
      if (same_)
      {
        interm_same_mat_density_[i].resize(natmol2_[i], std::vector<std::vector<double>>(natmol2_[i], std::vector<double>(cmdata::indexing::n_bins(cutoff_), 0)));
        interm_same_maxcdf_mol_[i].resize(natmol2_[i], std::vector<std::vector<double>>(natmol2_[i], std::vector<double>(cmdata::indexing::n_bins(cutoff_), 0)));
      }
      if (intra_) intram_mat_density_[i].resize(natmol2_[i], std::vector<std::vector<double>>(natmol2_[i], std::vector<double>(cmdata::indexing::n_bins(cutoff_), 0)));
      for ( std::size_t j = i + 1; j < natmol2_.size() && cross_; j++ )
      {
        interm_cross_mat_density_[i].resize(natmol2_[i], std::vector<std::vector<double>>(natmol2_[j], std::vector<double>(cmdata::indexing::n_bins(cutoff_), 0)));
        interm_cross_maxcdf_mol_[i].resize(natmol2_[i], std::vector<std::vector<double>>(natmol2_[j], std::vector<double>(cmdata::indexing::n_bins(cutoff_), 0)));
        cross_index_[i][j] = cross_count;
        cross_count++;
      }
    }

    if (sym_file_path_=="") printf("No symmetry file provided. Running with standard settings.\n");
    else printf("Running with symmetry file %s\nReading file...\n", sym_file_path_.c_str());
    cmdata::io::read_symmetry_indices(sym_file_path_, *mtop_, equivalence_list_, natmol2_, start_index);

    if (list_sym_)
    {
      printf("Writing out symmetry listing into %s\n", "sym_list.txt");
      std::fstream sym_list_file("sym_list.txt", std::fstream::out);
      for (int i = 0; i < equivalence_list_.size(); i++)
      {
        sym_list_file << "[ molecule_" << i << " ]\n";
        for ( int j = 0; j < equivalence_list_[i].size(); j++ )
        {
          sym_list_file << "atom " << j << ":";
          for ( int k = 0; k < equivalence_list_[i][j].size(); k++ )
          {
            sym_list_file << " " << equivalence_list_[i][j][k];
          }
          sym_list_file << "\n";
        }
        sym_list_file << "\n";
      }
      sym_list_file << "\n";
      sym_list_file.close();
    }

    n_bins_ = cmdata::indexing::n_bins(cutoff_);
    dx_ = cutoff_ / static_cast<double>(n_bins_);

    mcut2_ = mol_cutoff_ * mol_cutoff_;
    cut_sig_2_ = (cutoff_ + 0.02) * (cutoff_ + 0.02);
    snew(xcm_, nindex_);

    if (same_) frame_same_mat_.resize(natmol2_.size());
    if (intra_ | same_) frame_same_mutex_.resize(natmol2_.size());
    if (cross_) frame_cross_mat_.resize(cross_index_.size());
    if (cross_) frame_cross_mutex_.resize(cross_index_.size());
    std::size_t sum_cross_mol_sizes = 0;
    for ( std::size_t i = 0; i < natmol2_.size(); i++ )
    {
      if (same_) frame_same_mat_[i].resize(natmol2_[i] *  natmol2_[i] * num_mol_unique_[i], 0);
      if (intra_ | same_) frame_same_mutex_[i] = std::vector<std::mutex>(natmol2_[i] *  natmol2_[i]);
      for ( std::size_t j = i+1; j < natmol2_.size() && cross_; j++ )
      {
        frame_cross_mat_[cross_index_[i][j]].resize(natmol2_[i] * natmol2_[j] * num_mol_unique_[i] * num_mol_unique_[j], 0);
        frame_cross_mutex_[cross_index_[i][j]] = std::vector<std::mutex>(natmol2_[i] * natmol2_[j]);
      }
    }

    if ( weights_path_ != "" )
    {
      printf("Weights file provided. Reading weights from %s\n", weights_path_.c_str());
      weights_ = cmdata::io::read_weights_file(weights_path_);
      printf("Found %li frame weights in file\n", weights_.size());
      double w_sum = std::accumulate(std::begin(weights_), std::end(weights_), 0.0, std::plus<>());
      printf("Sum of weights amounts to %lf\n", w_sum);
      weights_sum_ = 0.;
    }

    printf("Finished preprocessing. Starting frame-by-frame analysis.\n");
  }

  void run()
  {
    std::cout << "Running frame-by-frame analysis" << std::endl;
    int frnr = 0;
    float progress = 0.0, new_progress = 0.0;
    cmdata::io::print_progress_bar(progress);
    while (frame_->read_next_frame(trj_) == exdrOK)
    {
      new_progress = static_cast<float>(frnr) / static_cast<float>(frame_->nframe);
      if (new_progress - progress > 0.01)
      {
        progress = new_progress;
        cmdata::io::print_progress_bar(progress);
      }

      if ((std::fmod(frame_->time, dt_) == 0) && (nskip_ == 0) || ((nskip_ > 0) && ((frnr % nskip_) == 0)))
      {
        double weight = 1.0;
        if ( !weights_.empty() )
        {
          weight = weights_[frnr];
          weights_sum_ += weight;
        }
        /* resetting the per frame vector to zero */
        for ( std::size_t i = 0; i < frame_same_mat_.size(); i++ )
        {
          #pragma omp parallel for num_threads(std::min(num_threads_, static_cast<int>(frame_same_mat_[i].size())))
          for ( std::size_t j = 0; j < frame_same_mat_[i].size(); j++ ) frame_same_mat_[i][j] = 100.;
        }
        for ( std::size_t i = 0; i < frame_cross_mat_.size(); i++ )
        {
          #pragma omp parallel for num_threads(std::min(num_threads_, static_cast<int>(frame_cross_mat_[i].size())))
          for ( std::size_t j = 0; j < frame_cross_mat_[i].size(); j++ ) frame_cross_mat_[i][j] = 100.;
        }

        /* start loop for each molecule */
        for (int i = 0; i < nindex_; i++)
        {
          /* start molecule thread*/
          mol_threads_[i] = std::thread(molecule_routine, i, nindex_, pbc_, frame_->x, std::cref(inv_num_mol_), 
          cut_sig_2_, std::cref(natmol2_), std::cref(num_mol_unique_), std::cref(mol_id_), std::cref(cross_index_),
          std::cref(density_bins_), mcut2_, xcm_, mols_, mtop_, std::cref(equivalence_list_), std::ref(frame_same_mat_),
          std::ref(frame_same_mutex_), std::ref(intram_mat_density_), std::ref(interm_same_mat_density_), std::ref(frame_cross_mat_), 
          std::ref(frame_cross_mutex_), std::ref(interm_cross_mat_density_), std::ref(semaphore_), std::cref(f_intra_mol_),
          std::cref(f_inter_mol_same_), std::cref(f_inter_mol_cross_), weight);
          /* end molecule thread */
        }
        /* join molecule threads */
        for ( auto &thread : mol_threads_ ) thread.join();

        /* calculate the mindist accumulation indices */
        std::size_t num_ops_same = 0;
        for ( std::size_t im = 0; im < natmol2_.size(); im++ ) num_ops_same += num_mol_unique_[im] * ( natmol2_[im] * ( natmol2_[im] + 1 ) ) / 2;
        int n_per_thread_same = (same_) ? num_ops_same / num_threads_  : 0;
        int n_threads_same_uneven = (same_) ? num_ops_same % num_threads_ : 0;
        std::size_t start_mti_same = 0, start_im_same = 0, end_mti_same = 1, end_im_same = 1; 
        std::size_t start_i_same = 0, start_j_same = 0, end_i_same = 0, end_j_same = 0;
        int num_ops_cross = 0;
        for ( std::size_t im = 0; im < natmol2_.size(); im++ )
        {
          for ( std::size_t jm = im + 1; jm < natmol2_.size(); jm++ )
          {
            num_ops_cross += num_mol_unique_[im] * natmol2_[im] * num_mol_unique_[jm] * natmol2_[jm];
          }
        }
        int n_per_thread_cross = (cross_) ? num_ops_cross / num_threads_ : 0;
        int n_threads_cross_uneven = (cross_) ? num_ops_cross % num_threads_ : 0;

        std::size_t start_mti_cross = 0, start_mtj_cross = 1, start_im_cross = 0, start_jm_cross = 0, start_i_cross = 0, start_j_cross = 0;
        std::size_t end_mti_cross = 1, end_mtj_cross = 2, end_im_cross = 1, end_jm_cross = 1, end_i_cross = 0, end_j_cross = 0;

        for ( int tid = 0; tid < num_threads_; tid++ )
        {
          /* calculate same indices */
          int n_loop_operations_same = n_per_thread_same + (tid < n_threads_same_uneven ? 1 : 0);
          long int n_loop_operations_total_same = n_loop_operations_same;
          while ( natmol2_[end_mti_same - 1] - static_cast<int>(end_j_same) <= n_loop_operations_same )
          {
            int sub_same = natmol2_[end_mti_same - 1] - static_cast<int>(end_j_same);
            n_loop_operations_same -= sub_same;
            end_i_same++;
            end_j_same = end_i_same;
            if ( static_cast<int>(end_j_same) == natmol2_[end_mti_same - 1] )
            {
              end_im_same++;
              end_i_same = 0;
              end_j_same = 0;
            }
            if ( static_cast<int>(end_im_same) == num_mol_unique_[end_mti_same - 1] )
            {
              end_mti_same++;
              end_im_same = 1;
              end_i_same = 0;
              end_j_same = 0;
            }
            if (n_loop_operations_same == 0) break;
          }
          end_j_same += n_loop_operations_same;  
          /* calculate cross indices */
          int n_loop_operations_total_cross = n_per_thread_cross + ( tid < n_threads_cross_uneven ? 1 : 0 );
          if ( natmol2_.size() > 1 && cross_ )
          {
            int n_loop_operations_cross = n_loop_operations_total_cross;
            while ( natmol2_[end_mti_cross-1] * natmol2_[end_mtj_cross-1] - (natmol2_[end_mtj_cross-1] * static_cast<int>(end_i_cross) + static_cast<int>(end_j_cross)) <= n_loop_operations_cross )
            {
              int sub_cross = natmol2_[end_mti_cross-1] * natmol2_[end_mtj_cross-1] - (natmol2_[end_mtj_cross-1] * static_cast<int>(end_i_cross) + static_cast<int>(end_j_cross));
              n_loop_operations_cross -= sub_cross;

              end_jm_cross++;
              end_i_cross = 0;
              end_j_cross = 0;

              // case jm is above max
              if ( end_jm_cross > num_mol_unique_[end_mtj_cross - 1] )
              {
                end_im_cross++;
                end_jm_cross = 1;
                end_i_cross = 0;
                end_j_cross = 0;
              }
              // case im is above max
              if ( end_im_cross > num_mol_unique_[end_mti_cross - 1] )
              {
                end_mtj_cross++;
                end_im_cross = 1;
                end_jm_cross = 1;
                end_i_cross = 0;
                end_j_cross = 0;
              }
              // case mtj is above max
              if ( end_mtj_cross > natmol2_[end_mtj_cross - 1] )
              {
                end_mti_cross++;
                end_mtj_cross = end_mti_cross + 1;
                end_im_cross = 1;
                end_jm_cross = 1;
                end_i_cross = 0;
                end_j_cross = 0;
              }
              if ( n_loop_operations_cross == 0 ) break;
              if ( end_mti_cross == natmol2_.size() - 1 ) break;
            }

            // calculate overhangs and add them
            end_i_cross += n_loop_operations_cross / natmol2_[end_mtj_cross-1];
            end_j_cross += n_loop_operations_cross % natmol2_[end_mtj_cross-1];
            end_i_cross += end_j_cross / natmol2_[end_mtj_cross-1];
            end_j_cross %= natmol2_[end_mtj_cross-1];
          }
          /* start thread */
          threads_[tid] = std::thread(
            mindist_kernel, weight, std::cref(natmol2_), std::cref(density_bins_), std::cref(num_mol_unique_), 
            start_mti_same, start_im_same, start_i_same, start_j_same, n_loop_operations_total_same,
            std::cref(frame_same_mat_), std::ref(frame_same_mutex_), std::ref(interm_same_maxcdf_mol_),
            start_mti_cross, start_mtj_cross, start_im_cross, start_jm_cross, start_i_cross, start_j_cross,
            n_loop_operations_total_cross, std::cref(cross_index_), std::cref(frame_cross_mat_), 
            std::ref(frame_cross_mutex_), std::ref(interm_cross_maxcdf_mol_)
          );
          /* end thread */

          /* set new starts */
          start_mti_same = end_mti_same - 1;
          start_im_same = end_im_same - 1;
          start_i_same = end_i_same;
          start_j_same = end_j_same;

          start_mti_cross = end_mti_cross - 1;
          start_mtj_cross = end_mtj_cross - 1;
          start_im_cross = end_im_cross - 1;
          start_jm_cross = end_jm_cross - 1;
          start_i_cross = end_i_cross;
          start_j_cross = end_j_cross;
        }
        for ( auto &thread : threads_ ) thread.join();
        ++n_x_;
      }
      ++frnr;
    }

    cmdata::io::print_progress_bar(1.0);
  }

  void process_data()
  {
    std::cout << "Finished frame-by-frame analysis\n";
    std::cout << "Analyzed " << n_x_ << " frames\n";
    std::cout << "Normalizing data... " << std::endl;
    // normalisations
    double norm = ( weights_.empty() ) ? 1. / n_x_ : 1. / weights_sum_;

    using ftype_norm = cmdata::ftypes::function_traits<decltype(&cmdata::density::normalize_histo)>;
    std::function<ftype_norm::signature> f_empty = cmdata::ftypes::do_nothing<ftype_norm>();

    std::function<ftype_norm::signature> normalize_intra = (intra_) ? cmdata::density::normalize_histo : f_empty;
    std::function<ftype_norm::signature> normalize_inter_same = (same_) ? cmdata::density::normalize_histo : f_empty;
    std::function<ftype_norm::signature> normalize_inter_cross = (cross_) ? cmdata::density::normalize_histo : f_empty;

    for (std::size_t i = 0; i < natmol2_.size(); i++)
    {
      for (int ii = 0; ii < natmol2_[i]; ii++)
      {
        for (int jj = ii; jj < natmol2_[i]; jj++)
        {
          double inv_num_mol_same = inv_num_mol_unique_[i];
          normalize_inter_same(i, ii, jj, norm, inv_num_mol_same, interm_same_maxcdf_mol_);
          normalize_inter_same(i, ii, jj, norm, 1.0, interm_same_mat_density_);
          normalize_intra(i, ii, jj, norm, 1.0, intram_mat_density_);

          double sum = 0.0;
          for ( std::size_t k = (same_) ? 0 : cmdata::indexing::n_bins(cutoff_); k < cmdata::indexing::n_bins(cutoff_); k++ )
          {
            sum+= dx_ * interm_same_maxcdf_mol_[i][ii][jj][k];
            if ( sum > 1.0 ) sum=1.0;
            interm_same_maxcdf_mol_[i][ii][jj][k] = sum;
          }
          if (same_) interm_same_mat_density_[i][jj][ii] = interm_same_mat_density_[i][ii][jj];
          if (same_) interm_same_maxcdf_mol_[i][jj][ii] = interm_same_maxcdf_mol_[i][ii][jj];
          if (intra_) intram_mat_density_[i][jj][ii] = intram_mat_density_[i][ii][jj];
        }
      }
      for (std::size_t j = i + 1; j < natmol2_.size() && cross_; j++)
      {
        for (int ii = 0; ii < natmol2_[i]; ii++)
        {
          for (int jj = 0; jj < natmol2_[j]; jj++)
          {
            double inv_num_mol_cross = inv_num_mol_unique_[i];
            normalize_inter_cross(cross_index_[i][j], ii, jj, norm, 1.0, interm_cross_mat_density_);
            normalize_inter_cross(cross_index_[i][j], ii, jj, norm, inv_num_mol_cross, interm_cross_maxcdf_mol_);

            double sum = 0.0;
            for ( std::size_t k = (cross_) ? 0 : cmdata::indexing::n_bins(cutoff_); k < cmdata::indexing::n_bins(cutoff_); k++ )
            {
              sum += dx_ * interm_cross_maxcdf_mol_[cross_index_[i][j]][ii][jj][k];
              if ( sum > 1.0 ) sum = 1.0;
              interm_cross_maxcdf_mol_[cross_index_[i][j]][ii][jj][k] = sum;
            }
          }
        }
      }
    }
  }
  
  void write_output( const std::string &output_prefix )
  {
    std::cout << "Writing data... " << std::endl;
    using ftype_write_intra = cmdata::ftypes::function_traits<decltype(&cmdata::io::f_write_intra)>;
    using ftype_write_inter_same = cmdata::ftypes::function_traits<decltype(&cmdata::io::f_write_inter_same)>;
    using ftype_write_inter_cross = cmdata::ftypes::function_traits<decltype(&cmdata::io::f_write_inter_cross)>;
    std::function<ftype_write_intra::signature> write_intra = cmdata::ftypes::do_nothing<ftype_write_intra>();
    std::function<ftype_write_inter_same::signature> write_inter_same = cmdata::ftypes::do_nothing<ftype_write_inter_same>();
    std::function<ftype_write_inter_cross::signature> write_inter_cross = cmdata::ftypes::do_nothing<ftype_write_inter_cross>();
    if (intra_) write_intra = cmdata::io::f_write_intra;
    if (same_) write_inter_same = cmdata::io::f_write_inter_same;
    if (cross_) write_inter_cross = cmdata::io::f_write_inter_cross;

    for (std::size_t i = 0; i < natmol2_.size(); i++)
    {
      std::cout << "Writing data for molecule " << i << "..." << std::endl;
      cmdata::io::print_progress_bar(0.0);
      float progress = 0.0, new_progress = 0.0;    
      for (int ii = 0; ii < natmol2_[i]; ii++)
      {
        new_progress = static_cast<float>(ii) / static_cast<float>(natmol2_[i]);
        if (new_progress - progress > 0.01)
        {
          progress = new_progress;
          cmdata::io::print_progress_bar(progress);
        }
        write_intra(output_prefix, i, ii, density_bins_, natmol2_, intram_mat_density_);
        write_inter_same(output_prefix, i, ii, density_bins_, natmol2_, interm_same_mat_density_, interm_same_maxcdf_mol_);
      }
      for (std::size_t j = i + 1; j < natmol2_.size(); j++)
      {
        for (int ii = 0; ii < natmol2_[i]; ii++)
        {
          write_inter_cross(output_prefix, i, j, ii, density_bins_, natmol2_, cross_index_, interm_cross_mat_density_, interm_cross_maxcdf_mol_);
        }
      }
    }
    cmdata::io::print_progress_bar(1.0);
    std::cout << "\nFinished!" << std::endl;
  }
};


} // namespace cmdata

#endif // _CM_DATA_HPP