#include <linux/pfn.h>
#include <asm/xen/page.h>
#include <asm/xen/hypercall.h>
#include <xen/interface/memory.h>
#include <linux/vmalloc.h>

#include "multicalls.h"
#include "mmu.h"

/*
 * Protects atomic reservation decrease/increase against concurrent increases.
 * Also protects non-atomic updates of current_pages and balloon lists.
 */
DEFINE_SPINLOCK(xen_reservation_lock);

unsigned long arbitrary_virt_to_mfn(void *vaddr)
{
	xmaddr_t maddr = arbitrary_virt_to_machine(vaddr);

	return PFN_DOWN(maddr.maddr);
}

xmaddr_t arbitrary_virt_to_machine(void *vaddr)
{
	unsigned long address = (unsigned long)vaddr;
	unsigned int level;
	pte_t *pte;
	unsigned offset;

	/*
	 * if the PFN is in the linear mapped vaddr range, we can just use
	 * the (quick) virt_to_machine() p2m lookup
	 */
	if (virt_addr_valid(vaddr))
		return virt_to_machine(vaddr);

	/* otherwise we have to do a (slower) full page-table walk */

	pte = lookup_address(address, &level);
	BUG_ON(pte == NULL);
	offset = address & ~PAGE_MASK;
	return XMADDR(((phys_addr_t)pte_mfn(*pte) << PAGE_SHIFT) + offset);
}
EXPORT_SYMBOL_GPL(arbitrary_virt_to_machine);

static void xen_flush_tlb_all(void)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

	trace_xen_mmu_flush_tlb_all(0);

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_TLB_FLUSH_ALL;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

#define REMAP_BATCH_SIZE 16

struct remap_data {
	xen_pfn_t *mfn;
	bool contiguous;
	pgprot_t prot;
	struct mmu_update *mmu_update;
};

static int remap_area_mfn_pte_fn(pte_t *ptep, pgtable_t token,
				 unsigned long addr, void *data)
{
	struct remap_data *rmd = data;
	pte_t pte = pte_mkspecial(mfn_pte(*rmd->mfn, rmd->prot));

	/* If we have a contiguous range, just update the mfn itself,
	   else update pointer to be "next mfn". */
	if (rmd->contiguous)
		(*rmd->mfn)++;
	else
		rmd->mfn++;

	rmd->mmu_update->ptr = virt_to_machine(ptep).maddr | MMU_NORMAL_PT_UPDATE;
	rmd->mmu_update->val = pte_val_ma(pte);
	rmd->mmu_update++;

	return 0;
}

static int do_remap_gfn(struct vm_area_struct *vma,
			unsigned long addr,
			xen_pfn_t *gfn, int nr,
			int *err_ptr, pgprot_t prot,
			unsigned domid,
			struct page **pages)
{
	int err = 0;
	struct remap_data rmd;
	struct mmu_update mmu_update[REMAP_BATCH_SIZE];
	unsigned long range;
	int mapped = 0;

	BUG_ON(!((vma->vm_flags & (VM_PFNMAP | VM_IO)) == (VM_PFNMAP | VM_IO)));

	rmd.mfn = gfn;
	rmd.prot = prot;
	/* We use the err_ptr to indicate if there we are doing a contiguous
	 * mapping or a discontigious mapping. */
	rmd.contiguous = !err_ptr;

	while (nr) {
		int index = 0;
		int done = 0;
		int batch = min(REMAP_BATCH_SIZE, nr);
		int batch_left = batch;
		range = (unsigned long)batch << PAGE_SHIFT;

		rmd.mmu_update = mmu_update;
		err = apply_to_page_range(vma->vm_mm, addr, range,
					  remap_area_mfn_pte_fn, &rmd);
		if (err)
			goto out;

		/* We record the error for each page that gives an error, but
		 * continue mapping until the whole set is done */
		do {
			int i;

			err = HYPERVISOR_mmu_update(&mmu_update[index],
						    batch_left, &done, domid);

			/*
			 * @err_ptr may be the same buffer as @gfn, so
			 * only clear it after each chunk of @gfn is
			 * used.
			 */
			if (err_ptr) {
				for (i = index; i < index + done; i++)
					err_ptr[i] = 0;
			}
			if (err < 0) {
				if (!err_ptr)
					goto out;
				err_ptr[i] = err;
				done++; /* Skip failed frame. */
			} else
				mapped += done;
			batch_left -= done;
			index += done;
		} while (batch_left);

		nr -= batch;
		addr += range;
		if (err_ptr)
			err_ptr += batch;
		cond_resched();
	}
out:

	xen_flush_tlb_all();

	return err < 0 ? err : mapped;
}

int xen_remap_domain_gfn_range(struct vm_area_struct *vma,
			       unsigned long addr,
			       xen_pfn_t gfn, int nr,
			       pgprot_t prot, unsigned domid,
			       struct page **pages)
{
	return do_remap_gfn(vma, addr, &gfn, nr, NULL, prot, domid, pages);
}
EXPORT_SYMBOL_GPL(xen_remap_domain_gfn_range);

int xen_remap_domain_gfn_array(struct vm_area_struct *vma,
			       unsigned long addr,
			       xen_pfn_t *gfn, int nr,
			       int *err_ptr, pgprot_t prot,
			       unsigned domid, struct page **pages)
{
	/* We BUG_ON because it's a programmer error to pass a NULL err_ptr,
	 * and the consequences later is quite hard to detect what the actual
	 * cause of "wrong memory was mapped in".
	 */
	BUG_ON(err_ptr == NULL);
	return do_remap_gfn(vma, addr, gfn, nr, err_ptr, prot, domid, pages);
}
EXPORT_SYMBOL_GPL(xen_remap_domain_gfn_array);

/* Returns: 0 success */
int xen_unmap_domain_gfn_range(struct vm_area_struct *vma,
			       int numpgs, struct page **pages)
{
	if (!pages || !xen_feature(XENFEAT_auto_translated_physmap))
		return 0;

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(xen_unmap_domain_gfn_range);

/* Note: here 'mfn' is actually gfn!!! */
struct vm_struct * xen_remap_domain_mfn_range_in_kernel(unsigned long mfn,
		int nr, unsigned domid)
{
	struct vm_struct *area;
	struct remap_data rmd;
	struct mmu_update mmu_update[REMAP_BATCH_SIZE];
	int batch;
	unsigned long range, addr;
	pgprot_t prot;
	int err;

	WARN_ON(in_interrupt() || irqs_disabled());

	area = alloc_vm_area(nr << PAGE_SHIFT, NULL);
	if (!area)
		return NULL;

	addr = (unsigned long)area->addr;

	prot = __pgprot(pgprot_val(PAGE_KERNEL));
	rmd.mfn = &mfn;
	rmd.prot = prot;
	rmd.contiguous = true;

	while (nr) {
		batch = min(REMAP_BATCH_SIZE, nr);
		range = (unsigned long)batch << PAGE_SHIFT;

		rmd.mmu_update = mmu_update;
		err = apply_to_page_range(&init_mm, addr, range,
					  remap_area_mfn_pte_fn, &rmd);
		if (err || HYPERVISOR_mmu_update(mmu_update, batch, NULL, domid) < 0)
			goto err;

		nr -= batch;
		addr += range;
	}

	xen_flush_tlb_all();
	return area;
err:
	free_vm_area(area);
	xen_flush_tlb_all();
	return NULL;
}
EXPORT_SYMBOL_GPL(xen_remap_domain_mfn_range_in_kernel);

void xen_unmap_domain_mfn_range_in_kernel(struct vm_struct *area, int nr,
		unsigned domid)
{
	struct remap_data rmd;
	struct mmu_update mmu_update;
	unsigned long range, addr = (unsigned long)area->addr;
#define INVALID_MFN (~0UL)
	unsigned long invalid_mfn = INVALID_MFN;
	int err;

	WARN_ON(in_interrupt() || irqs_disabled());

	rmd.prot = PAGE_NONE;

	while (nr) {
		range = (unsigned long)(1 << PAGE_SHIFT);

		rmd.mfn = &invalid_mfn;
		rmd.mmu_update = &mmu_update;
		err = apply_to_page_range(&init_mm, addr, range,
					  remap_area_mfn_pte_fn, &rmd);
		BUG_ON(err);
		BUG_ON(HYPERVISOR_mmu_update(&mmu_update, 1, NULL, domid) < 0);

		nr--;
		addr += range;
	}

	free_vm_area(area);
	xen_flush_tlb_all();
}
EXPORT_SYMBOL_GPL(xen_unmap_domain_mfn_range_in_kernel);
